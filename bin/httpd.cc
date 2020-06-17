// #include "types.h"
// #include "user.h"
// #include "lib.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include "sockutil.h"

#include "sysstubs.h"

#define VERSION "0.1"
#define HTTP_VERSION "1.0"
#define BUFSIZE 512

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#define SOCK_STREAM 1
#define AF_INET 2
#define INADDR_ANY 0
typedef uint32_t socklen_t;
struct in_addr {
  uint32_t s_addr;
};
struct sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};
unsigned long htonl(unsigned long x) {
  return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | (x >> 24);
}
unsigned short htons(unsigned short x) {
  return ((x & 0xff) << 8) | ((x & 0xff00) >> 8);
}

static int xwrite(int fd, const void *buf, u64 n)
{
  int r;
  
  while (n) {
    r = ward_write(fd, buf, n);
    if (r < 0 || r == 0) {
      fprintf(stderr, "xwrite: failed %d\n", r);
      return -1;
    }
    buf = (char *) buf + r;
    n -= r;
  }

  return 0;
}

static void
error(int s, int code)
{
  static struct {
    int code;
    const char *msg;
  } errors[] = {
    { 400, "Bad Request" },
    { 404, "Page Not Found" },
    { 500, "Internal Server Error" },
  };
  
  char buf[512];
  int i;
  int r;

  for (i = 0; i < NELEM(errors); i++)
    if (errors[i].code == code)
      break;

  if (i == NELEM(errors)) {
    fprintf(stderr, "httpd error: unknown code %u", code);
    exit(-1);
  }

  snprintf(buf, 512, "HTTP/" HTTP_VERSION" %d %s\r\n"
           "Server: xv6-httpd/" VERSION "\r\n"
           "Connection: close\r\n"
           "Content-type: text/html\r\n"
           "\r\n"
           "<html><body><p>%d - %s</p></body></html>\r\n",
           errors[i].code, errors[i].msg, 
           errors[i].code, errors[i].msg);
  r = strlen(buf);

  if (xwrite(s, buf, r))
    fprintf(stderr, "httpd error: incomplete write\n");
}

static int
header(int s)
{
  static const char *h = "HTTP/" HTTP_VERSION " 200 OK\r\n"
    "Server: xv6-httpd/" VERSION "\r\n";

  int len;

  len = strlen(h);
  if (xwrite(s, h, len)) {
    fprintf(stderr, "httpd header: incomplete write");
    exit(-1);
  }

  return 0;
}

static int
header_fin(int s)
{
  static const char *f = "\r\n";
  
  int len;

  len = strlen(f);
  if (xwrite(s, f, len)) {
    fprintf(stderr, "httpd fin: incomplete write");
    exit(-1);
  }

  return 0;
}

static int
content_length(int s, u64 size)
{
  char buf[128];
  int len;

  snprintf(buf, 128, "Content-Length: %lu\r\n", size);
  len = strlen(buf);

  if (xwrite(s, buf, len)) {
    fprintf(stderr, "httpd size: incomplete write");
    exit(-1);
  }

  return 0;
}

static int
content_type(int s)
{
  static const char *t = "Content-Type: text/plain\r\n";

  int len;

  len = strlen(t);
  if (xwrite(s, t, len)) {
    fprintf(stderr, "httpd content_type: incomplete write");
    exit(-1);
  }

  return 0;
}

static int
content(int s, int fd)
{
  char buf[256];
  int n;

  for (;;) {
    n = ward_read(fd, buf, sizeof(buf));
    if (n < 0) {
      fprintf(stderr, "send_data: read failed %d\n", n);
      return n;
    } else if (n == 0) {
      return 0;
    }
    
    if (xwrite(s, buf, n) < 0) {
      fprintf(stderr, "httpd content: write failed\n");
      return -1;
    }
  }
}

static void
resp_get(int s, const char *url)
{
  struct stat stat;
  int fd;
  int r;

  fd = open(url, O_RDONLY);
  if (fd < 0) {
    error(s, 404);
    return;
  }

  r = fstat(fd, &stat);
  if (r < 0) {
    fprintf(stderr, "httpd resp: fstat %d\n", r);
    ward_close(fd);
    return error(s, 404);
  }

  // if (!S_ISREG(stat.st_mode) && !S_ISCHR(stat.st_mode)) {
  //   fprintf(stderr, "httpd resp: bad kind st_mode=%d\n", stat.st_mode);
  //   ward_close(fd);
  //   return error(s, 404);
  // }

  r = header(s);
  if (r < 0)
    goto error;

  if (stat.st_size != 0) {
    r = content_length(s, stat.st_size);
    if (r < 0)
      goto error;
  }

  r = content_type(s);
  if (r < 0)
    goto error;

  r = header_fin(s);
  if (r < 0)
    goto error;

  r = content(s, fd);
  if (r < 0)
    goto error;
  
  ward_close(fd);
  return;

error:
  ward_close(fd);
  error(s, 500);
}

static void
resp_put(int s, const char *url, int content_length)
{
  int r;

  if (content_length < 0) {
    error(s, 400);
    return;
  }

  int fd = ward_open(url, O_WRONLY|O_CREAT|O_TRUNC);
  if (fd < 0) {
    error(s, 404);
    return;
  }

  char buf[1024];
  while (content_length && (r = ward_read(s, buf, NELEM(buf))) != 0) {
    if (r < 0) {
      fprintf(stderr, "httpd client: read %d\n", r);
      r = -400;
      goto error;
    }
    content_length -= r;
    r = xwrite(fd, buf, r);
    if (r < 0) {
      fprintf(stderr, "httpd client: write %d\n", r);
      r = -400;
      goto error;
    }
  }

  r = header(s);
  if (r < 0)
    goto error;

  r = header_fin(s);
  if (r < 0)
    goto error;

  ward_close(fd);
  return;

 error:
  ward_close(fd);
  error(s, 500);
}

static int
readline(int s, char *buf, int limit)
{
  char *pos = buf;
  while (pos < buf + limit - 1) {
    int r = ward_read(s, pos, 1);
    if (r < 0)
      return r;
    if (r == 0)
      break;
    ++pos;
    if (*(pos - 1) == '\n')
      break;
  }
  *pos = '\0';
  return pos - buf;
}

static int
parse(const char *b, char **rurl, const char **method)
{
  const char *url;
  int len;
  char *r;

  *method = NULL;
  if (strncmp(b, "GET ", 4) == 0)
    *method = "GET";
  else if (strncmp(b, "PUT ", 4) == 0)
    *method = "PUT";
  else
    return -1;

  b += 4;
  url = b;
  while (*b && *b != ' ')
    b++;
  len = b - url;

  r = (char *) malloc(len+1);
  if (r == nullptr)
    return -1;
  memmove(r, url, len);
  r[len] = 0;

  *rurl = r;
  return 0;
}

static void
client(int s)
{
  char b[BUFSIZE];
  char *url;
  const char *method;
  int r;
  int content_length = -1;

  r = readline(s, b, NELEM(b));
  if (r < 0) {
    fprintf(stderr, "httpd client: read request %d\n", r);
    return;
  }

  r = parse(b, &url, &method);
  if (r < 0) {
    error(s, 400);
    return;
  }

  do {
    r = readline(s, b, NELEM(b));
    if (r <= 0) {
      fprintf(stderr, "httpd client: read headers %d\n", r);
      return;
    }
    if (strncmp(b, "Content-Length: ", 16) == 0)
      content_length = atoi(b + 16);
  } while (strcmp(b, "\r\n"));

  fprintf(stderr, "httpd client: %s %s\n", method, url);
  if (method[0] == 'G')
    resp_get(s, url);
  else if (method[0] == 'P')
    resp_put(s, url, content_length);
  free(url);
}

int
main(void)
{
  int s;
  int r;

  s = ward_socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    fprintf(stderr, "httpd socket: %d\n", s);
    exit(-1);
  }

  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(80);
  r = ward_bind(s, (struct sockaddr *)&sin, sizeof(sin));
  if (r < 0) {
    fprintf(stderr, "httpd bind: %d\n", r);
    exit(-1);
  }

  r = ward_listen(s, 5);
  if (r < 0) {
    fprintf(stderr, "httpd listen: %d\n", r);
    exit(-1);
  }

  fprintf(stderr, "httpd: port 80\n");

  for (;;) {
    socklen_t socklen;
    int ss;

    socklen = sizeof(sin);
    ss = ward_accept(s, (struct sockaddr *)&sin, &socklen);
    if (ss < 0) {
      fprintf(stderr, "httpd accept: %d\n", ss);
      continue;
    }
    fprintf(stderr, "httpd: connection ???\n" /*, ipaddr(&sin)*/);

    client(ss);
    ward_close(ss);
  }
}
