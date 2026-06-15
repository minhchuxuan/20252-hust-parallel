#!/usr/bin/env bash
# bench/_lib.sh - shared helpers for the benchmark scripts. Source it, don't run.
#
#   median           : read numbers on stdin, print their median ("" if none)
#   field KEY        : read a driver summary line on stdin, print the numeric
#                      value of the "KEY=<number>s" token (e.g. field time)

median() {
  sort -n | awk '
    BEGIN { c = 0 }
    { a[c++] = $0 }
    END {
      if (c == 0) { print ""; exit }
      if (c % 2) print a[int(c/2)];
      else       print (a[c/2 - 1] + a[c/2]) / 2
    }'
}

field() {
  awk -v k="$1" '
    { for (i = 1; i <= NF; i++)
        if ($i ~ "^" k "=") { v = $i; gsub(k "=|s$", "", v); print v } }'
}
