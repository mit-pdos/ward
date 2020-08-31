#!/bin/sh

echo Running getpid...
getpid || (echo FAILED && halt 1)
echo OK
echo

echo Running anon...
anon 100000 || (echo FAILED && halt 1)
echo OK
echo

echo Running schedbench...
schedbench || (echo FAILED && halt 1)
echo OK
echo

echo Running lebench...
lebench - 20 || (echo FAILED && halt 1)
echo OK

halt 0
