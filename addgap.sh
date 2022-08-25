#!/bin/sh

for f in `ls allwaves/*.wav`
do
  fn=`echo $f | sed -e 's:^.*/::'`
  echo $fn
  sox allwaves/0101-gap0_1.wav allwaves/$fn gapwaves/$fn
done
