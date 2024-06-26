# VocTool

This project provides a tool to convert WAVE file into 8bit Creative VOC files, including ADPCM compression.
Currently 2bit and 4bit ADPCM are supported.

The resulting VOC file is always in mono.

## Usage of encoder

~~~
Usage: voctool -i INPUT [ -f FREQUENCY ] -o OUTPUT [ -c COMPRESSION ] [ -n NORMALIZE ] [ -l LEVEL ] 

Program to convert WAVE files into VOC files including optional ADPCM compression.
File is converted to mono. If a frequency is give then the file is also resampled
to the given frequency. Otherwise the sample frequency of the WAVE file is kept.

Compression formats:
  PCM    - unsigned integer 8-bit per sample
  ADPCM4 - ADPCM 4-bit per sample
  ADPCM2 - ADPCM 2-bit per sample


options:
  -i, --input        Name of the input file
  -f, --frequency    Frequency of output file in hertz ( default: -1 )
  -o, --output       Name of the output file
  -c, --compression  Compression to be used. Options: PCM, ADPCM4, ADPCM2 ( default: ADPCM4 )
  -n, --normalize    Normalize audio to given fraction, e.g. 0.9 ( default: -1.0 )
  -l, --level        Level of compression. Must be integer. 1 = lowest quality but fast. Bigger values than 5 probably make no sense and are terribly slow. ( default: 4 )
~~~

Example usage:
~~~
voctool -i sound.wav -f 8000 -c ADPCM4 -o sound.voc -n 0.7
~~~
This will encode the file **sound.wav** into a Creative ADPCM encoded VOC file with 8kHz.
It will also normalize the sound to 70% of the maximum volume.


## About Creative ADPCM

Creative ADPCM compresses an 8bit per sample sound file into a 4bit/2bit per sample sound file.
This halves/quaters the required space, but also decreases the sound quality.
This works by only storing the difference to the previous sample.

## About this encoder

I wrote this encoder because I needed a Creative ADPCM encoder for my MSDOS platform game.
At first I looked for already existing encoders, but there actually were no free encoders available.
The only (non free) encoder I could find was the tool VOCEDIT 2 (see http://www.vogons.org/viewtopic.php?t=8634).
Unfortunately that tool only runs on DOS and it is also a GUI tool that does not support scripting.
I actually got VOCEDIT 2 to run using Dosbox but because of the fact that it is not scriptable and also not free, I decided to build an encoder myself.


