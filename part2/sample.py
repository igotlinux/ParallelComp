# create_sample_pcap.py
with open("sample.pcap", "wb") as f:
    # some random data
    f.write(b"\x01\x02\x03hello world\x04\x05")
    # embed GET /admin
    f.write(b"GET /admin HTTP/1.1\r\nHost: example.com\r\n\r\n")
    # embed USER/PASS
    f.write(b"USER root\r\nPASS 1234\r\n")
    # embed SQL injection
    f.write(b"Normal traffic DROP TABLE users; more data")
    # embed NOP-sled
    f.write(b"\x90\x90\x90\x90\x90")
    # add some random traffic
    f.write(b"\xAA\xBB\xCC\xDD")
print("sample.pcap created with embedded signatures")
