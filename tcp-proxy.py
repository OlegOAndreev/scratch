#!/usr/bin/python

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
    listenSocket = None
    toHost = None
    toPort = None
    toFamily = None
    socketsToRead = []
    # This map will contain both sockets (from client and from proxied server).
    socketMap = {}

    def __init__(self, fromPort, fromFamily, toHost, toPort, toFamily):
        self.listenSocket = socket.socket(fromFamily, socket.SOCK_STREAM)
        self.listenSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if fromFamily == socket.AF_INET6:
            # Otherwise the bind will bind to both ipv4 and ipv6 sockets.
            self.listenSocket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        self.listenSocket.setblocking(False)
        self.listenSocket.bind(("", fromPort))
        self.listenSocket.listen(MAX_LISTEN)
        self.toHost = toHost
        self.toPort = toPort
        self.toFamily = toFamily
        self.socketsToRead.append(self.listenSocket)

    def run_forever(self):
        while True:
            socketsReady, _, _ = select.select(self.socketsToRead, [], [])
            for readySocket in socketsReady:
                if readySocket == self.listenSocket:
                    self.acceptIncoming()
                else:
                    self.readWrite(readySocket)

    def acceptIncoming(self):
        try:
            fromSocket, _ = self.listenSocket.accept()
            printLog("Connection from {0}".format(fromSocket.getpeername()))
        except socket.error, e:
            printErr("Got exception when accepting: {0}".format(str(e)))

        try:
            toSocket = socket.socket(self.toFamily, socket.SOCK_STREAM)
            toSocket.connect((self.toHost, self.toPort))
            printLog("Proxy connection: {0} -> {1}".format(fromSocket.getpeername(), toSocket.getpeername()))
        except socket.error as e:
            printErr("Got exception when connecting to {0}:{1} : {2}".format(self.toHost, self.toPort, e))
            fromSocket.close()
            return
        self.socketMap[fromSocket] = toSocket
        self.socketMap[toSocket] = fromSocket
        self.socketsToRead.append(fromSocket)
        self.socketsToRead.append(toSocket)
    
    def cleanSockets(self, socket1, socket2):
        socket2.close()
        del self.socketMap[socket1]
        del self.socketMap[socket2]
        self.socketsToRead.remove(socket1)
        self.socketsToRead.remove(socket2)

    def readWrite(self, readSocket):
        writeSocket = self.socketMap[readSocket]
        try:
            data = readSocket.recv(MAX_BUFFER)
        except socket.error as e:
            printErr("Got exception when reading data in {0} -> {1}: {2}".format(readSocket.getpeername(), writeSocket.getpeername(), e))
            self.cleanSockets(readSocket, writeSocket)
            return
        if not data:
            printLog("Host {0} closed connection, closing {1}".format(readSocket.getpeername(), writeSocket.getpeername()))
            self.cleanSockets(readSocket, writeSocket)
            return
        try:
            writeSocket.send(data)
        except socket.error as e:
            printErr("Got exception when writing data in {0} -> {1}: {2}".format(readSocket.getpeername(), writeSocket.getpeername(), e))
            self.cleanSockets(writeSocket, readSocket)
            return

def parse_family(s):
    if s.endswith("6"):
        return socket.AF_INET6
    elif s.endswith("4"):
        return socket.AF_INET
    else:
        printErr("Unknown family {0}".format(s))
        sys.exit(1)

def family_to_str(family):
    if family == socket.AF_INET6:
        return "ipv6"
    elif family == socket.AF_INET:
        return "ipv4"
    else:
        printErr("Unknown family {0}".format(family))
        sys.exit(1)

if len(sys.argv) != 6:
    printErr("Usage: {0} listen-port listen-family to-host to-port to-family".format(sys.argv[0]))
    sys.exit(1)

listenPort = int(sys.argv[1])
listenFamily = parse_family(sys.argv[2])
toHost = sys.argv[3]
toPort = int(sys.argv[4])
toFamily = parse_family(sys.argv[5])
printLog("Running proxy {0} ({1}) -> {2}:{3} ({4})".format(listenPort, family_to_str(listenFamily), toHost, toPort, family_to_str(toFamily)))

proxyServer = ProxyServer(listenPort, listenFamily, toHost, toPort, toFamily)
proxyServer.run_forever()