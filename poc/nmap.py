import sys
import os

def exec_task(str):
	cmd="nmap -sS -O %s"%str
	output=os.popen(cmd)
	return output.read()
