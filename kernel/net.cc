#include "types.h"
#include "kernel.hh"
#include "spinlock.hh"
#include "condvar.hh"
#include "proc.hh"
#include "fs.h"
#include "file.hh"
#include "net.hh"
#include "major.h"
#include "netdev.hh"

#include "lwip/tcpip.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/sockets.h"
#include "netif/etharp.h"

err_t if_init(struct netif *netif);
void if_input(struct netif *netif, void *buf, u16 len);

netdev *the_netdev;

void
netfree(void *va)
{
  kfree(va);
}

void *
netalloc(void)
{
  return kalloc("(netalloc)");
}

int
nettx(void *va, u16 len)
{
  if (!the_netdev)
    return -1;
  return the_netdev->transmit(va, len);
}

void
nethwaddr(u8 *hwaddr)
{
  if (!the_netdev)
    return;
  the_netdev->get_hwaddr(hwaddr);
}

class file_lwip_socket : public refcache::referenced, public file
{
  int socket_;
  semaphore wsem_, rsem_;

  ~file_lwip_socket()
  {
    lwip_core_lock();
    lwip_close(socket_);
    lwip_core_unlock();
  }

public:
  file_lwip_socket(int socket)
    : socket_(socket), wsem_("file_lwip_socket::wsem", 1),
      rsem_("file_lwip_socket::rsem", 1) { }
  NEW_DELETE_OPS(file_lwip_socket);

  void inc() override { referenced::inc(); }
  void dec() override { referenced::dec(); }

  ssize_t read(userptr<void> addr, size_t n) override
  {
    char b[PGSIZE];
    if (n > PGSIZE)
      n = PGSIZE;

    auto l = rsem_.guard();
    lwip_core_lock();
    int ret = lwip_read(socket_, b, n);
    lwip_core_unlock();

    if (ret <= 0 || addr.store_bytes(b, ret))
      return ret;
    return -1;
  }

  ssize_t write(const userptr<void> data, size_t n) override
  {
    char buf[PGSIZE];
    if (n > PGSIZE)
      n = PGSIZE;
    if (!data.load_bytes(buf, n))
      return -1;

    auto l = wsem_.guard();
    lwip_core_lock();
    int r = lwip_write(socket_, buf, n);
    lwip_core_unlock();
    return r;
  }

  int bind(const struct ward_sockaddr *addr, size_t addrlen) override
  {
    sockaddr a;
    if (addr->sa_family == WARD_AF_INET) {
      a.sa_family = AF_INET;
    } else if(addr->sa_family == WARD_AF_INET6) {
      a.sa_family = AF_INET6;
    } else {
      return -1;
    }
    memcpy(a.sa_data, addr->sa_data, 14);
    a.sa_len = sizeof(sockaddr);

    lwip_core_lock();
    int r = lwip_bind(socket_, &a, addrlen);
    lwip_core_unlock();
    return r;
  }

  int listen(int backlog) override
  {
    lwip_core_lock();
    int r = lwip_listen(socket_, backlog);
    lwip_core_unlock();
    return r;
  }

  int accept(struct ward_sockaddr_storage* addr, size_t *addrlen, file **out)
    override
  {
    ward_sockaddr_storage a;

    lwip_core_lock();
    socklen_t len = sizeof(*addr);
    int ss = lwip_accept(socket_, (struct sockaddr*)&a, &len);
    lwip_core_unlock();
    if (ss < 0)
      return -1;
    *addrlen = len;
    *out = new file_lwip_socket{ss};

    memcpy(((ward_sockaddr*)addr)->sa_data, ((sockaddr*)&a)->sa_data, 14);
    if (((sockaddr*)&a)->sa_family == AF_INET) {
      ((ward_sockaddr*)addr)->sa_family = WARD_AF_INET;
    } else if(((sockaddr*)&a)->sa_family == AF_INET6) {
      ((ward_sockaddr*)addr)->sa_family = WARD_AF_INET6;
    } else {
      panic("unknown address family");
    }

    return 0;
  }

  void onzero() override
  {
    delete this;
  }
};

static struct netif nif;

struct timer_thread {
  u64 nsec;
  struct condvar waitcv;
  struct spinlock waitlk;
  void (*func)(void);
};

int errno;

void
netrx(void *va, u16 len)
{
  lwip_core_lock();
  if_input(&nif, va, len);
  lwip_core_unlock();
}

static void __attribute__((noreturn))
net_timer(void *x)
{
  struct timer_thread *t = (struct timer_thread *) x;

  for (;;) {
    u64 cur = nsectime();
    
    lwip_core_lock();
    t->func();
    lwip_core_unlock();
    acquire(&t->waitlk);
    t->waitcv.sleep_to(&t->waitlk, cur + t->nsec);
    release(&t->waitlk);
  }
}

static void
start_timer(struct timer_thread *t, void (*func)(void),
            const char *name, u64 msec)
{
  t->nsec = 1000000000 / 1000*msec;
  t->func = func;
  t->waitcv = condvar(name);
  t->waitlk = spinlock(name, LOCKSTAT_NET);
  threadrun(net_timer, t, name);
}

static void
lwip_init(struct netif *xnif, void *if_state,
	  u32 init_addr, u32 init_mask, u32 init_gw)
{
  ip4_addr_t ipaddr, netmask, gateway;
  ipaddr.addr = init_addr;
  netmask.addr = init_mask;
  gateway.addr = init_gw;
  
  if (0 == netif_add(xnif, &ipaddr, &netmask, &gateway,
                     if_state,
                     if_init,
                     ip_input))
    panic("lwip_init: error in netif_add\n");
  
  netif_set_default(xnif);
  netif_set_up(xnif);
}

static void
tcpip_init_done(void *arg)
{
  volatile long *tcpip_done = (volatile long*) arg;
  *tcpip_done = 1;
}

static int
netifread(char *dst, u32 off, u32 n)
{
  u32 ip, nm, gw;
  char buf[512];
  u32 len;

  // TODO: handle printing ipv6 addresses
  assert(nif.ip_addr.type == IPADDR_TYPE_V4);
  assert(nif.netmask.type == IPADDR_TYPE_V4);
  assert(nif.gw.type == IPADDR_TYPE_V4);

  ip = ntohl(nif.ip_addr.u_addr.ip4.addr);
  nm = ntohl(nif.netmask.u_addr.ip4.addr);
  gw = ntohl(nif.gw.u_addr.ip4.addr);

#define IP(x)              \
  (x & 0xff000000) >> 24, \
  (x & 0x00ff0000) >> 16, \
  (x & 0x0000ff00) >> 8,  \
  (x & 0x000000ff)

  snprintf(buf, sizeof(buf),
           "hw %02x:%02x:%02x:%02x:%02x:%02x\n"
           "ip %u.%u.%u.%u nm %u.%u.%u.%u gw %u.%u.%u.%u\n",
           nif.hwaddr[0], nif.hwaddr[1], nif.hwaddr[2],
           nif.hwaddr[3], nif.hwaddr[4], nif.hwaddr[5],
           IP(ip), IP(nm), IP(gw));

#undef IP

  len = strlen(buf);

  if (off >= len)
    return 0;

  n = MIN(len - off, n);
  memmove(dst, &buf[off], n);
  return n;
}

static void
initnet_worker(void *x)
{
  static struct timer_thread t_arp, t_tcpf, t_tcps, t_dhcpf, t_dhcpc;
  volatile long tcpip_done = 0;

  lwip_core_init();

  lwip_core_lock();
  tcpip_init(&tcpip_init_done, (void*)&tcpip_done);
  lwip_core_unlock();
  while (!tcpip_done)
    yield();

  lwip_core_lock();
  memset(&nif, 0, sizeof(nif));
  lwip_init(&nif, nullptr, 0, 0, 0);

  dhcp_start(&nif);

  start_timer(&t_arp, &etharp_tmr, "arp_timer", ARP_TMR_INTERVAL);
  start_timer(&t_dhcpf, &dhcp_fine_tmr,	"dhcp_f_timer",	DHCP_FINE_TIMER_MSECS);
  start_timer(&t_dhcpc, &dhcp_coarse_tmr, "dhcp_c_timer", DHCP_COARSE_TIMER_MSECS);

#if 1
  lwip_core_unlock();
#else
  // This DHCP code is useful for debugging, but isn't necessary
  // for the lwIP DHCP client.
  struct spinlock lk("dhcp sleep");
  struct condvar cv("dhcp cv sleep");
  int dhcp_state = 0;
  const char *dhcp_states[] = {
    [DHCP_RENEWING]  = "renewing",
    [DHCP_SELECTING] = "selecting",
    [DHCP_CHECKING]  = "checking",
    [DHCP_BOUND]     = "bound",
  };

  for (;;) {
    if (dhcp_state != nif.dhcp->state) {
      dhcp_state = nif.dhcp->state;
      cprintf("net: DHCP state %d (%s)\n", dhcp_state,
              dhcp_states[dhcp_state] ? : "unknown");

      if (dhcp_state == DHCP_BOUND) {
        u32 ip = ntohl(nif.ip_addr.addr);
        cprintf("net: %02x:%02x:%02x:%02x:%02x:%02x" 
                " bound to %u.%u.%u.%u\n", 
                nif.hwaddr[0], nif.hwaddr[1], nif.hwaddr[2],
                nif.hwaddr[3], nif.hwaddr[4], nif.hwaddr[5],
                (ip & 0xff000000) >> 24,
                (ip & 0x00ff0000) >> 16,
                (ip & 0x0000ff00) >> 8,
                (ip & 0x000000ff));
      }
    }

    lwip_core_unlock();    
    acquire(&lk);
    cv.sleepto(&lk, nsectime() + 1000000000);
    release(&lk);
    lwip_core_lock();
  }
#endif
}

void
initnet(void)
{
  devsw[MAJ_NETIF].pread = netifread;
  threadrun(initnet_worker, nullptr, "initnet");
}

int
netsocket(int domain, int type, int protocol, file **out)
{
  int r;
  lwip_core_lock();
  r = lwip_socket(domain, type, protocol);
  lwip_core_unlock();
  if (r < 0)
    return -1;
  *out = new file_lwip_socket{r};
  return 0;
}
