#!/bin/bash
# compare_run.sh
FILE=${1:-sample.pcap}
SIG=signatures.txt
START=0
LENGTH=100000

echo "Running scalar C++ scan..."
/usr/bin/time -f "SCALAR_TIME %e sec, %M KB maxrss" ./scalar_scan $SIG $FILE $START $LENGTH out_scalar.json 2>&1 | sed -n '1,2p'

if [ -x ./avx_scan ]; then
  echo
  echo "Running AVX C++ scan..."
  /usr/bin/time -f "AVX_TIME %e sec, %M KB maxrss" ./avx_scan $SIG $FILE $START $LENGTH out_avx.json 2>&1 | sed -n '1,2p'
else
  echo "avx_scan binary not found; skipping AVX run."
fi

echo
echo "Running Python scan..."
/usr/bin/time -f "PY_TIME %e sec, %M KB maxrss" python3 python_scan.py $SIG $FILE $START $LENGTH out_py.json 2>&1 | sed -n '1,2p'

echo
echo "Comparing JSON outputs..."
python3 - <<PY
import json,sys,subprocess
def load(p): 
    try:
        return json.load(open(p))
    except:
        print("Missing or invalid",p); sys.exit(1)
s=load("out_scalar.json")
p=load("out_py.json")
if s==p:
    print("scalar == python: OK")
else:
    print("scalar != python: DIFF")
if __import__('os').path.exists("out_avx.json"):
    a=load("out_avx.json")
    if s==a:
        print("scalar == avx: OK")
    else:
        print("scalar != avx: DIFF")
PY
