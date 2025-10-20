# create_big_sample.sh
#!/bin/bash

OUTFILE="big_sample.bin"
rm -f $OUTFILE

echo "Creating 2GB big sample file with random data and embedded signatures..."
for i in $(seq 1 2048); do
    # add 1 MB of random data per iteration
    dd if=/dev/urandom bs=1M count=1 >> $OUTFILE 2>/dev/null
    
    # sprinkle signatures every 10 MB
    if [ $((i % 10)) -eq 0 ]; then
        echo "GET /admin_random_$i" >> $OUTFILE
        echo "DROP TABLE users_random_$i" >> $OUTFILE
        echo "USER root_$i" >> $OUTFILE
        echo "PASS 1234_$i" >> $OUTFILE
    fi
done

ls -lh $OUTFILE
echo "Done!"
