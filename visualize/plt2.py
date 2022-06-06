import numpy as np
import matplotlib.pyplot as plt

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [0.1365, 2.9033, 6.1566, 11.6784]
plt.subplot(1, 2, 1)
plt.bar(x, a, label='read', color='blue')
plt.ylabel(r'(ms)')
plt.legend()

x = ['sync', 'io_uring', 'px_aio', 'libaio']
a = [0.1764, 6.8054, 7.0417, 11.676]
plt.subplot(1, 2, 2)
plt.bar(x, a, label='write', color='red')
plt.ylabel(r'(ms)')
plt.legend()

plt.subplots_adjust(wspace=0.3, hspace=0.3)
plt.savefig('fig2.jpg', dpi=1080)