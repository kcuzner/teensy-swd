"""
Data contracts for usb communication

If a DTO can be read, a static read() method is supplied which takes in array
with the binary data. If a DTO can be written, it provides an instance write()
method which returns a binary array with the packed data.
"""

import struct

def to_number(val):
    if isinstance(val, str):
        return int(val,
            0 if val.startswith('0x') or val.startswith('0b') else 16)
    return val

class ReadRequest(object):
    """
    Request for an SWD read
    """
    FORMAT = "B"
    def __init__(self, request=0xa5):
        request = to_number(request)
        self.request = request
    def write(self):
        return struct.pack(ReadRequest.FORMAT, self.request)

class WriteRequest(object):
    """
    Request for an SWD write
    """
    FORMAT = "BxxxI"
    def __init__(self, request, data):
        request = to_number(request)
        data = to_number(data)
        self.request = request
        self.data = data
    def write(self):
        return struct.pack(WriteRequest.FORMAT, self.request, self.data)

class CommandResult(object):
    """
    Result of an SWD command
    """
    FORMAT = "BbxxI"
    @staticmethod
    def read(arr):
        data = struct.unpack(CommandResult.FORMAT, arr)
        return CommandResult(data[0], data[1], data[2])
    def __init__(self, done, result, data):
        self.done = done
        self.result = result
        self.data = data
    def __str__(self):
        return "Result:\nDone: {0}\nResult: {1}\nData: {2}"\
            .format(self.done, self.result, self.data)
