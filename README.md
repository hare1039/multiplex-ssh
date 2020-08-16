# wait-until
This program helps you run a command when a keyword or regex appears.


```
./build-mac/bin/wait-until --help
Allowed options:
  --help                produce help message
  --run arg             Monitor this command
  --regex arg           Wait until this regex shows up
  --command arg         Run this command when keyword shows up
```

# example

```
$ cat socat-chisel.sh
wait-until --run "chisel client --keepalive 30s https://ssh.hare1039.nctu.me/ 10000:localhost:22" \
           --regex '.*Connected.*' -- \
           socat - TCP-CONNECT:127.0.0.1:10000,RETRY=600
```

Run `chisel client --keepalive 30s https://ssh.hare1039.nctu.me/ 10000:localhost:22`

When 'Connected' appeared in stdout, run `socat - TCP-CONNECT:127.0.0.1:10000,RETRY=600`

Now you can connect your ssh server through a http server: `ssh -o "ProxyCommand=socat-chisel.sh" hare1039@127.0.0.1`

