#!/bin/sh
BZIP=$1
set -e
$BZIP -1  < sample1.ref > sample1.rb2
cmp sample1.bz2 sample1.rb2
$BZIP -2  < sample2.ref > sample2.rb2
cmp sample2.bz2 sample2.rb2
$BZIP -3  < sample3.ref > sample3.rb2
cmp sample3.bz2 sample3.rb2
$BZIP -d  < sample1.bz2 > sample1.tst
cmp sample1.tst sample1.ref
$BZIP -d  < sample2.bz2 > sample2.tst
cmp sample2.tst sample2.ref
$BZIP -ds < sample3.bz2 > sample3.tst
cmp sample3.tst sample3.ref
