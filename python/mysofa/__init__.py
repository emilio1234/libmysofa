from ctypes import *
from numpy.ctypeslib import ndpointer
import numpy as np
############### lib definitions ###############
mysofa_lib = cdll.LoadLibrary("libmysofa.so")

mysofa_open = mysofa_lib.mysofa_open
mysofa_open.restype = c_void_p
mysofa_open.argtypes = [c_char_p, c_float, POINTER(c_int), POINTER(c_int)]

mysofa_close = mysofa_lib.mysofa_close
mysofa_close.argtypes = [c_void_p]

mysofa_getfilter_float = mysofa_lib.mysofa_getfilter_float
mysofa_getfilter_float.argtypes = [c_void_p, c_float, c_float, c_float,
                                   ndpointer(c_float, flags="C_CONTIGUOUS"),
                                   ndpointer(c_float, flags="C_CONTIGUOUS"),
                                   POINTER(c_float), POINTER(c_float)]
############### lib definitions ###############


class MySofa:
    def __init__(self, filename, rate):
        filter_length = c_int()
        err = c_int()
        self._handle = mysofa_open(filename.encode(), rate, byref(filter_length), byref(err))

        self.filter_length = filter_length.value
        self.error = err.value

        self.ir_left = np.zeros(self.filter_length, dtype=np.float32)
        self.ir_right = np.zeros(self.filter_length, dtype=np.float32)
        self.delay_left = 0.0
        self.delay_right = 0.0

        pass

    def set_filter(self, x, y, z):
        delay_left = c_float()
        delay_right = c_float()
        mysofa_getfilter_float(self._handle, x, y, z, self.ir_left, self.ir_right,
                               byref(delay_left), byref(delay_right))

        self.delay_left = delay_left.value
        self.delay_right = delay_right.value

    def apply(self, inp):
        left = np.convolve(inp, self.ir_left,'same')
        right = np.convolve(inp, self.ir_right,'same')
        return np.vstack((left, right))

    def close(self):
        mysofa_close(self._handle)


if __name__ == "__main__":

    msof = MySofa("../share/MIT_KEMAR_normal_pinna.sofa", 16000.0)
    print(msof.filter_length, msof.error)
    msof.set_filter(-1.0, 0.5, 5.0)
    print(msof.ir_left, msof.ir_right, msof.delay_left, msof.delay_right)
    msof.close()
    import matplotlib.pyplot as plt
    plt.figure()
    plt.plot(msof.ir_left)
    plt.plot(msof.ir_right)
    plt.show()

