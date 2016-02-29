# WASAPITest
Simple testbed for WASAPI api on Windows platform

There are probably tonnes of error cases not handled but I wanted some simple code to start understanding WASAPI and there wasn't any. Hopefully this can be of help to someone! Currently contains three projects.

### Audio generation

Uses a sine wave to output a stereo tone. Frequency can be varied using UP and DOWN arrows and ESC to exit.

### RAW playback

I grabbed the first WAV file I found on my harddrive and converted it to a variety of RAW formats using Audacity.
If you switch the format of the Raw file it is loading you will need to manually change the code (search for HARDCODED_FORMAT).

### OGG playback

Uses <a href='https://github.com/nothings/stb/blob/master/stb_vorbis.c'>stb_vorbis</a> to decode an OGG file into PCM data. The data is then played back in a similar manner to the RAW playback.


# Thanks

Sean Barrett and contributers for <a href='https://github.com/nothings/stb/blob/master/stb_vorbis.c'>stb_vorbis</a>

bone666138 of Freesound.org for creating this alarm clock sample: <a href='https://www.freesound.org/people/bone666138/sounds/198841/'>198841</a>.

