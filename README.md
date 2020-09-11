# multiplex-ssh
This program helps you combine multiple tcp connection into 1.

Typical scenario is making a ssh server that do not allow TCP Forwarding to forward your connection.

# Binaries
This repo contains 2 programs: `multiplex-tcp` and `remote`

Please put the `remote` on the remote ssh server

# example
```
# on no.forward.ssh.server.com
# run an http proxy on 8000
./glider -listen 8000

# on local machine run
# This execute the 'ssh no.forward.ssh.server.com ./remote --to localhost:8000'
# And the remote ssh server will run             './remote --to localhost:8000'
./multiplex-tcp --run 'ssh no.forward.ssh.server.com ./remote --to localhost:8000' --listen 3128

# now you can have your proxy ready on :3128 on your local machine
curl -x http://localhost:3128 ifconfig.me
```

