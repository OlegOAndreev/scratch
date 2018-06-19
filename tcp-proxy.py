#!/usr/bin/python

import argparse
import datetime
import select
import socket
import sys

# A small script for proxying requests to some port (either ipv4 or ipv6) to some other host/port (either ipv4 or ipv6).
# Usage:
#  tcp-proxy.py 12345 v6 127.0.0.1 12345 v4 - will proxy connections to :::12345 to 127.0.0.1:12345
#  (useful if you have a legacy service listening only on ipv4)

MAX_LISTEN = 100
MAX_BUFFER = 10000

def printLog(s):
    sys.stdout.write(str(datetime.datetime.now()))
    sys.stdout.write(" INFO: ")
    sys.stdout.write(s)
    sys.stdout.write('\n')
    sys.stdout.flush()

# Write both INFO and ERROR to stdout
def printErr(s):
    sys.stdout.write(str(datetime.datetime.now()))
    sys.stdout.write(" ERROR: ")
    sys.stdout.write(s)
    sys.stdout.write('\n')
    sys.stdout.flush()

class ProxyServer:
    to_host = None
    to_family = None
    # This maps listening socket, bound to local port to remote port number.
    listen_sockets = {}
    # This maps socket to peer, contains both local -> remote and remote -> local mappings.
    peer_map = {}

    def __init__(self, from_ports, from_family, to_host, to_ports, to_family):
        self.to_host = to_host
        self.to_family = to_family
        for (from_port, to_port) in zip(from_ports, to_ports):
            listen_socket = socket.socket(from_family, socket.SOCK_STREAM)
            listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if from_family == socket.AF_INET6:
                # Otherwise the bind will bind to both ipv4 and ipv6 sockets.
                listen_socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            # Recommended for dealing with errors when accept can hang in case of misbehaving client.
            listen_socket.setblocking(False)
            # Empty string is equivalent of INADDR_ANY
            listen_socket.bind(("", from_port))
            listen_socket.listen(MAX_LISTEN)
            self.listen_sockets[listen_socket] = to_port

    def run_forever(self):
        while True:
            sockets_to_read = list(self.listen_sockets.keys()) + list(self.peer_map.keys())
            sockets_ready, _, _ = select.select(sockets_to_read, [], [])
            for ready_socket in sockets_ready:
                if ready_socket in self.listen_sockets:
                    self.accept_incoming(ready_socket)
                else:
                    self.read_write(ready_socket)

    def accept_incoming(self, listen_socket):
        try:
            from_socket, _ = listen_socket.accept()
            printLog("Connection from {0}".format(from_socket.getpeername()))
        except socket.error, e:
            printErr("Got exception when accepting: {0}".format(str(e)))

        to_port = self.listen_sockets[listen_socket]
        try:
            to_socket = socket.socket(self.to_family, socket.SOCK_STREAM)
            to_socket.connect((self.to_host, to_port))
            printLog("Proxy connection: {0} -> {1}".format(from_socket.getpeername(), to_socket.getpeername()))
        except socket.error as e:
            printErr("Got exception when connecting to {0}:{1} : {2}".format(self.to_host, to_port, e))
            from_socket.close()
            return
        self.peer_map[from_socket] = to_socket
        self.peer_map[to_socket] = from_socket

    def read_write(self, read_socket):
        write_socket = self.peer_map[read_socket]
        try:
            data = read_socket.recv(MAX_BUFFER)
        except socket.error as e:
            printErr("Got exception when reading data in {0} -> {1}: {2}".format(read_socket.getpeername(), write_socket.getpeername(), e))
            self.clean_sockets(read_socket, write_socket)
            return
        if not data:
            printLog("Host {0} closed connection, closing {1}".format(read_socket.getpeername(), write_socket.getpeername()))
            self.clean_sockets(read_socket, write_socket)
            return
        try:
            write_socket.send(data)
        except socket.error as e:
            printErr("Got exception when writing data in {0} -> {1}: {2}".format(read_socket.getpeername(), write_socket.getpeername(), e))
            self.clean_sockets(write_socket, read_socket)
            return

    def clean_sockets(self, socket1, socket2):
        socket1.close()
        socket2.close()
        del self.peer_map[socket1]
        del self.peer_map[socket2]

def family_to_str(family):
    if family == socket.AF_INET6:
        return "ipv6"
    elif family == socket.AF_INET:
        return "ipv4"
    else:
        printErr("Unknown family {0}".format(family))
        sys.exit(1)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Proxies requests from some ports on ipv4/ipv6 to other ports on other host on ipv4/ipv6")
    ap.add_argument("--from-ports", dest="from_ports", required=True, help="List of ports to proxy from, separated by comma, must be matched --to-ports")
    ap.add_argument("--from-v4", dest="from_v4", action="store_true", help="Proxy from ipv4 ports, either this or --from-v6 is required")
    ap.add_argument("--from-v6", dest="from_v6", action="store_true", help="Proxy from ipv6 ports, either this or --from-v4 is required")
    ap.add_argument("--to-ports", dest="to_ports", help="List of ports to proxy to, separated by comma, must be matched by --from-ports"
                                                        " or be empty (in this case it will be equal to --from-ports")
    ap.add_argument("--to-v4", dest="to_v4", action="store_true", help="Proxy to ipv4 ports, either this or --to-v6 is required")
    ap.add_argument("--to-v6", dest="to_v6", action="store_true", help="Proxy to ipv6 ports, either this or --to-v4 is required")
    ap.add_argument("--to-host", dest="to_host", help="Host to proxy to, localhost by default")
    args = ap.parse_args()

    from_ports = [int(i.strip()) for i in args.from_ports.split(",")]
    if not from_ports:
        print("There must be at least one port in --from-ports")
        sys.exit(1)
    if args.to_ports:
        to_ports = [int(i.strip()) for i in args.to_ports.split(",")]
        if len(from_ports) != len(to_ports):
            print("Number of ports in --from-ports and --to-ports must match")
            sys.exit(1)
    else:
        to_ports = from_ports

    if (not args.from_v4 and not args.from_v6) or (args.from_v4 and args.from_v6):
        print("Either --from-v4 or --from-v6 is required")
        sys.exit(1)
    if args.from_v4:
        from_family = socket.AF_INET
    else:
        from_family = socket.AF_INET6

    if (not args.to_v4 and not args.to_v6) or (args.to_v4 and args.to_v6):
        print("Either --to-v4 or --to-v6 is required")
        sys.exit(1)
    if args.to_v4:
        to_family = socket.AF_INET
    else:
        to_family = socket.AF_INET6
    
    to_host = args.to_host
    if not to_host:
        if args.to_v4:
            to_host = "127.0.0.1"
        else:
            to_host = "::1"

	printLog("Running proxy {0} ({1}) -> {2}:{3} ({4})".format(from_ports, family_to_str(from_family), to_host, to_ports, family_to_str(to_family)))

	proxyServer = ProxyServer(from_ports, from_family, to_host, to_ports, to_family)
	proxyServer.run_forever()
