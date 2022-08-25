#import wave
from scipy.io.wavfile import write as wavwrite
import numpy as np
import matplotlib.pyplot as plt

dotlen = .06413
morsefreq = 1000.
# from 1992 JJY voice record
# (https://jjy.nict.go.jp/QandA/reference/ShortWave/leapsec_19920701_JJYand117.wav),
# but it's 8.8% faster tape record. :-)

srate = 44100
morseamp  = 16384
# adjust with the voice data on
# https://jjy.nict.go.jp/QandA/reference/JJYwav.html .

def sinwave(freq, len):
    return np.array(
        [int(np.sin(2 * np.pi * (i * morsefreq / srate)) * morseamp) \
                     for i in range(int(len * dotlen * srate))])\
                         .astype(np.int16)
    
def morseout(s):
    print('mourseout ', s, ': ', sep = '', end = '')
    makebreak = sum([code[c] for c in s], [])
    print(makebreak)
    ismake = True
    wavea = np.array([], dtype = np.int16)
    for i in makebreak:
        if ismake:
            wavea = np.append(wavea, sinwave(morsefreq, i))
            ismake = False
        else:
            wavea = np.append(wavea, np.array([0] * int(i * dotlen * srate)).\
                              astype(np.int16))
            ismake = True
#    x = [x for x in range(len(wavea))]
#    plt.plot(x, wavea)
#    print(len(wavea))
#    plt.show()
    wavwrite('c-' + s + '.wav', srate, wavea)
    return
    
code = {}
code['0'] = [3, 1, 3, 1, 3, 1, 3, 1, 3, 3]
code['1'] = [1, 1, 3, 1, 3, 1, 3, 1, 3, 3]
code['2'] = [1, 1, 1, 1, 3, 1, 3, 1, 3, 3]
code['3'] = [1, 1, 1, 1, 1, 1, 3, 1, 3, 3]
code['4'] = [1, 1, 1, 1, 1, 1, 1, 1, 3, 3]
code['5'] = [1, 1, 1, 1, 1, 1, 1, 1, 1, 3]
code['6'] = [3, 1, 1, 1, 1, 1, 1, 1, 1, 3]
code['7'] = [3, 1, 3, 1, 1, 1, 1, 1, 1, 3]
code['8'] = [3, 1, 3, 1, 3, 1, 1, 1, 1, 3]
code['9'] = [3, 1, 3, 1, 3, 1, 3, 1, 1, 3]
code['j'] = [1, 1, 3, 1, 3, 1, 3, 3]
code['y'] = [3, 1, 1, 1, 3, 1, 3, 3]
code['n'] = [3, 1, 1, 3]
code['u'] = [1, 1, 1, 1, 3, 3]
code['w'] = [1, 1, 3, 1, 3, 3]

for s in ['jjy', 'nnnnn', 'uuuuu', 'wwwww']:
    morseout(s)
for i in range(10):
    s = str(i);
    morseout(s)
#for i in range(6):
#    s = str(i) + '0';
#    morseout(s)
#for i in range(1, 24):
#    s = '{:02d}'.format(i)
#    morseout(s)
       
