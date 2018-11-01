# Minimal-SDR




[![Video](https://github.com/FrankBoesing/Minimal-SDR/blob/master/extras/1.png)](https://www.youtube.com/watch?v=VIKR3kuGEqg "First demo")


 Hardware:
 * MCP2036
   
  How does it work:
* Teensy produces LO RX frequency on BCLK (pin 9) or MCLK (pin 11) 
* MCP2036 mixes LO with RF coming from antenna --> direct conversion
* MCP amplifies and lowpass filters the IF signal 
* Teensy ADC samples incoming IF signal with sample rate == IF * 4
* Software Oscillators cos & sin are used to produce I & Q signals and simultaneously translate the I & Q signals to audio baseband
* I & Q are filtered by linear phase FIR [tbd]
* Demodulation --> SSB, AM or synchronous AM
* Decoding of time signals or other digital modes [tbd]
* auto-notch filter to eliminate birdies
* IIR biquad filter to shape baseband audio
* Audio output through Teensy DAC
