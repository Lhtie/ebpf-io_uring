import numpy as np
import matplotlib.pyplot as plt

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [5.676, 25.339, 6.887, 24.391]
plt.subplot(2, 2, 1)
plt.bar(x, a, label='random read', color='blue')
plt.ylabel(r'$\times 10^3$ IOPS')
plt.legend()

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [9.230, 176.313, 5.456, 143.955]
plt.subplot(2, 2, 2)
plt.bar(x, a, label='sequential read', color='red')
plt.ylabel(r'$\times 10^3$ IOPS')
plt.legend()

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [6.432, 21.622, 5.154, 20.240]
plt.subplot(2, 2, 3)
plt.bar(x, a, label='random write', color='yellow')
plt.ylabel(r'$\times 10^3$ IOPS')
plt.legend()

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [5.869, 12.838, 4.614, 18.703]
plt.subplot(2, 2, 4)
plt.bar(x, a, label='sequential write', color='green')
plt.ylabel(r'$\times 10^3$ IOPS')
plt.legend()

plt.subplots_adjust(wspace=0.3, hspace=0.3)
plt.savefig('fig.jpg', dpi=1080)