#!/usr/bin/env python3
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.

"""Run a filter connected to a real CUPS backend, the way cupsd does.

The filter's output is piped into the backend, and the two share a CUPS
side channel on file descriptor 4, so SNMP queries from the filter reach
the printer. Nothing has to be installed and no print queue is needed.

Usage:
  run_with_backend.py DEVICE_URI PPD FILTER [FILTER ARGS...]

Example (asks the printer for its supply levels; prints nothing):
  printf '#CUPS-COMMAND\\nReportLevels\\n' > /tmp/cmd
  run_with_backend.py socket://192.168.2.90:9100 build/ppd/hl2270dw.ppd \\
      build/commandtohl2270dw 1 user title 1 '' /tmp/cmd

Be careful: whatever the filter writes is sent to the printer.
"""

import os
import socket
import subprocess
import sys


def spawn_with_fd4(argv, sock, **kwargs):
    # Duplicate the side channel socket to fd 4 in the child.
    shell = ['/bin/sh', '-c', 'exec "$@" 4<&%d' % sock.fileno(), 'sh'] + argv
    return subprocess.Popen(shell, pass_fds=(sock.fileno(),), **kwargs)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    uri, ppd, filter_argv = sys.argv[1], sys.argv[2], sys.argv[3:]
    scheme = uri.split(':', 1)[0]
    backend = os.path.join('/usr/libexec/cups/backend', scheme)
    env = dict(os.environ, DEVICE_URI=uri, PPD=os.path.abspath(ppd))

    backend_end, filter_end = socket.socketpair(socket.AF_UNIX,
                                                socket.SOCK_STREAM)
    read_fd, write_fd = os.pipe()
    back = spawn_with_fd4([backend, '1', 'user', 'title', '1', ''],
                          backend_end, stdin=read_fd, env=env,
                          stderr=subprocess.PIPE, text=True)
    filt = spawn_with_fd4(filter_argv, filter_end, stdout=write_fd, env=env,
                          stderr=subprocess.PIPE, text=True)
    os.close(read_fd)
    os.close(write_fd)
    backend_end.close()
    filter_end.close()

    filter_err = filt.communicate(timeout=120)[1]
    backend_err = back.communicate(timeout=120)[1]
    print('--- filter (exit %d)' % filt.returncode)
    print(filter_err, end='')
    print('--- backend (exit %d)' % back.returncode)
    print(backend_err, end='')
    return filt.returncode or back.returncode


if __name__ == '__main__':
    sys.exit(main())
