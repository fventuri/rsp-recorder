# SDRplay RSP I/Q recorder utility

`rsp-recorder` is a command line utility to record to disk the stream of I/Q samples from a SDRplay RSP radio. It works both with single tuner models, like RSP1B and RSPdx(-R2), and the RSPduo dual tuner model.

The output formats are:
  - WavViewDX-raw - just the I/Q values as a stream of 16 bit shorts, i.e. I, Q for the single tuner case, and I tuner A, Q tuner A, I tuner B, Q tuner B for the dual tuner case
  - Linrad, compatible with the Linrad SDR program
  - SDRuno, which generates a WAV file in RIFF/RF64 format with two/four PCM channels compatible with SDRuno and WavViewDX
  - SDRconnect, which generates a WAV file in RIFF/RF64 format with two/four PCM channels compatible with SDRconnect and WavViewDX

Most of the RSP parameters available though the API can be set through command line arguments; these include center frequency, sample rate, decimation, IF frequency, IF bandwidth, gains, notch filters, and several others (see below).

In the dual tuner case, settings that should be different between the two tuners can be assigned by separating the values with a comma. For instance:
  - `-l 3` sets the LNA state for both tuners at 3
  - `-l 3,5` sets the LNA state for tuner A at 3 and for tuner B at 5

In the dual tuner case, there are some settings, like decimation, that accept only one value, since the two tuners should always use the same decimation factor (if not, the two output streams would have a different sample rate).

The files in SDRuno format contain a special chunk called 'auxi' that follows the same format used by SDRuno
The two values 'unused4' and 'unused5' in the 'auxi' chunk contain the initial gains in 1/1000 of a dB (a 'milli dB'); in other words a value of 57539 means a gain of 57.539 dB. These gains shouldn't change during a recording unless AGC is enabled (see 'gains file' below).

There also an experimental WAV RF64 format that can optionally contain time markers stored in a 'r64m' chunk, following the format described EBU technical specification 3306 v1.1 (July 2009). The command line argument '-m' enables these time markers at specified intervals; for instance '-m 60' creates a marker at the beginning of each minute; '-m 900' creates markers at 0, 15, 30, and 45 minutes past the hour. The labels for these time markers are the timestamps in ISO8601/RFC3339 format (including nanoseconds; for instance '2025-11-18T15:55:45.123456789Z')

The utility can also write a secondary file with the gain changes; anytime one of the gain values changes (because of AGC), a new entry is added to this file with:
   - sample number (uint64_t)
   - current gain (float)
   - tuner (uint8_t) - always 0 for the single tuner case; 0 for tuner A, 1 for tuner B in the dual tuner case
   - gRdB (uint8_t)
   - LNA gRdB  (uint8_t)
   - padding (1 byte)

Each entry is 16 bytes long; it can be read using Python struct module with a format '@Qf3Bx'. See the example Python script `show_gains.py` for more details.

## Important note about sample rates, IF frequency, and IF bandwidth when operating in low-IF mode (i.e. when the IF frequency is not 0). These notes also apply to the RSPduo in dual tuner mode and in master/slave mode.

To operate the RSP in low-IF mode or in dual tuner (and master/slave) mode in the case of the RSPduo, the hardware/software requires one of a specific set of combinations of sample rate, IF frequency, and IF bandwidth. The full list is shown in the table below. When one of these modes is selected, the RSP hw/sw will apply an 'internal decimation' (by 3 or 4) that will divide the RSP ADC sample rate. The output sample rate (i.e. the sample rate of the I/Q samples that this utility will write to file) is therefore:

    fs_out = RSP sample rate / internal decimation / user selected decimation

For instance with RSP sample rate=6MHz, IF frequency=1620kHz, IF bandwidth=1536kHz, and decimation=1, the output sample rate will be 2Msps (6M / 3 / 1).

The `-r` argument selects the value of the RSP ADC sample rate, i.e. the first column in the table below, not the output sample rate.

| RSP sample rate | IF frequency | IF bandwidth | Int decimation |  fs_out |
| :-------------: | :----------: | :----------: | :------------: | :-----: |
|     8192000     |    2048      |    1536      |       4        | 2048000 |
|     8000000     |    2048      |    1536      |       4        | 2000000 |
|     8000000     |    2048      |    5000      |       4        | 2000000 |
|     2000000     |     450      |     200      |       4        |  500000 |
|     2000000     |     450      |     300      |       4        |  500000 |
|     2000000     |     450      |     600      |       2        | 1000000 |
|     6000000     |    1620      |     200      |       3        | 2000000 |
|     6000000     |    1620      |     300      |       3        | 2000000 |
|     6000000     |    1620      |     600      |       3        | 2000000 |
|     6000000     |    1620      |    1536      |       3        | 2000000 |


## Output filename

The name of the output file containing the samples can be specified with the `-o` option. The output file name template supports the following 'variables', which will be replaced by their values when generating the actual file name:
  - {WAVVIEWDX-RAW} will be replaced by 'iq_pcm16_ch<num_channels>_cf<center_freq>_sr<sampling_rate>_dt<datetime>' following the filename convention required by WavViewDX for raw I/Q files
  - {SDRUNO} will be replaced by 'SDRuno_<UTC_datetime>_<center_freq_in_kHz>kHz' following the filename convention used by SDRuno
  - {SDRCONNECT} will be replaced by 'SDRconnect_IQ_<datetime>_<center_freq>HZ' following the filename convention used by SDRconnect
  - {FREQ} will be replaced by the center frequency in Hz
  - {FREQHZ} will be replaced by the center frequency in Hz followed by 'Hz'
  - {FREQKHZ} will be replaced by the center frequency in kHz followed by 'kHz'
  - {TIMESTAMP} will be replaced by the UTC timestamp at the beginning of the recording in 'YYYYMMDD_HHMMSS' format
  - {TSIS8601} will be replaced by the UTC timestamp at the beginning of the recording in IS08601 format
  - {LOCALTIME} will be replaced by the local time at the beginning of the recording in 'YYYYMMDD_HHMMSS+-TZOFFSET' format

For instance, an output filename specified as '{SDRCONNECT}.wav' could generate an output file with this actual name: SDRconnect_IQ_20251123_173458_800000HZ.wav
An output filename specified as '{WAVVIEWDX-RAW}.raw' could generate an output file with this actual name: iq_pcm16_ch1_cf800000_sr2000000_dt20251123-154313.raw


## Antenna names

- RSP2:
  - Antenna A
  - Antenna B
  - Hi-Z
- RSPduo:
  - Tuner 1 50 ohm
  - Tuner 2 50 ohm
  - High Z
  - Both Tuners (dual tuner mode only)
- RSPdx/RSPdx-R2:
  - Antenna A
  - Antenna B
  - Antenna C


## Build instructions

```
mkdir build
cd build
cmake ..
make (or ninja)
```


## Notes for Windows users

- the utility can be built with MinGW/MSYS2 (https://www.msys2.org/); MinGW can also be used to cross compile it on Linux to create a Windows executable
- the commands to build it are the same commands in the section 'Build instructions' above (in a msys2 terminal)
- alternatively you can download the precompiled binary for Windows in the 'Releases' page (https://github.com/fventuri/rsp-recorder/releases)
- in this case you'll probably need to 'unblock' it before you run it for the first time
- you will also need to have the SDRplay API DLL (`sdrplay_api.dll`) in a location where `rsp-recorder.exe` can find it, either by copying it to the same folder where you run `rsp-recorder.exe`, or by adding to the `PATH` environment variable the path of the folder where `sdrplay_api.dll` is located


## Usage

These are the command line options for `rsp-recorder`:

    -c <configuration file>
    -s <RSP serial number>
    -w <RSPduo mode> (1: single tuner, 2: dual tuner, 4: master, 8: slave)
    -a <antenna>
    -r <RSP sample rate>
    -d <decimation>
    -i <IF frequency>
    -b <IF bandwidth>
    -g <IF gain reduction> ("AGC" to enable AGC)
    -l <LNA state>
    -n <notch filter> (one of: RF, DAB, or RSPduo-AM)
    -D disable post tuner DC offset compensation (default: enabled)
    -I disable post tuner I/Q balance compensation (default: enabled)
    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)
    -B enable bias-T
    -H enable HDR mode for RSPdx and RSPdx-R2
    -u <HDR mode bandwidth> (0: 200kHz, 1: 500kHz, 2: 1200kHz, 3: 1700kHz)
    -f <center frequency>
    -x <streaming time (s)> (default: 10s)
    -m <time marker interval (s)> (default: 0 -> no time markers)
    -t <output file format> (one of: WavViewDX-raw, Linrad, SDRuno, SDRconnect, experimental)
    -o <output filename template>
    -z <zero sample gaps if smaller than size> (default: 100000)
    -j <blocks buffer capacity> (in number of blocks)
    -k <samples buffer capacity> (in number of samples)
    -G write gains file (default: disabled)
    -X enable SDRplay API debug log level (default: disabled)
    -v enable verbose mode (default: disabled)


## Configuration file(s)

All the settings for dual tuner recorder can be read from a configuration file in the format shown below.

Multiple configuration files can be specified by the using the `-c` argument multiple times in the command line. 

Arguments from the command line can also be used to add or change settings read from the configuration file; for instance most of the settings of the recording (like frequency, gains, sample rate, etc) could be loaded from the configuration file, but the actual recording duration could be specified on the command line via the `-x` argument.

### Configuration file format

Lines in the configuration file are just `setting_name = setting_value`, for instance `frequency = 100e6`. Lines that begin with `#` are comments and are ignored. Empty lines are ignored too.

An example of a simple configuration file is shown below.

These are the names for the settings:
  - `serial number`
  - `RSPduo mode`
  - `antenna`
  - `sample rate` (Or `RSP sample rate`)
  - `decimation`
  - `IF frequency`
  - `IF bandwidth`
  - `gRdB` (or `IFGR`)
  - `LNA state` (or `RFGR`)
  - `RF notch`
  - `DAB notch`
  - `RSPduo AM notch`
  - `DC offset correction` (or `DC corr`)
  - `IQ imbalance correction` (or `IQ corr`)
  - `DC offset dcCal` (or `dcCal`)
  - `DC offset speedUp` (or `speedUp`)
  - `DC offset trackTime` (or `trackTime`)
  - `DC offset refreshRateTime` (or `refreshRateTime`)
  - `Bias-T` (or `BiasT`)
  - `HDR mode`
  - `HDR mode bandwidth`
  - `frequency`
  - `streaming time`
  - `marker interval`
  - `output type`
  - `output file`
  - `gain file`
  - `zero sample gaps max size`
  - `blocks buffer capacity`
  - `samples buffer capacity`
  - `gain changes buffer capacity`
  - `verbose`

### Configuration file examples

Simple configuration file for recording with RSPdx in HDR mode
```
# configuration file for HDR mode recording with RSPdx (compatible with SDRconnect and WavViewDX)
# Franco Venturi - Thu Nov 20 07:52:16 AM EST 2025

sample rate = 6000000
IF frequency = 1620
IF bandwidth = 600
gRdB = AGC
LNA state = 0
HDR mode = true
frequency = 875000
output type = SDRconnect
output file = mw/{SDRCONNECT}.wav
```

Simple configuration file for MW recording with RSPduo in dual tuner mode
```
# configuration file for MW recording (Linrad output format)
# Franco Venturi - Thu Nov 20 07:54:07 AM EST 2025

sample rate = 6000000
IF frequency = 1620
IF bandwidth = 1536
gRdB = AGC
LNA state = 0
frequency = 800000
output type = Linrad
output file = mw/RSPduo_dual_tuner_{TIMESTAMP}_{FREQHZ}.raw
```


## Examples

### single tuner mode (non RSPduo)

These examples are for RSP1, RSP1A, RSP1B, RSP2, RSPdx, RSPdx-R2

- record local NOAA weather radio on 162.55MHz for 10 seconds using an RSP sample rate of 6MHz and IF=1620kHz (and LNA state set to 3):
```
rsp-recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000
```
The output file will have a sample rate of 2Msps.

- same as above, but with a sample rate of 8MHz (lower sample resolution; 12 bits instead of 14 bits)
```
rsp-recorder -r 8000000 -i 2048 -b 1536 -l 3 -f 162550000
```
In this case too, the output file will have a sample rate of 2Msps.

 - same as the first example, but with IF AGC enabled and streaming for 5 minutes:
```
rsp-recorder -r 6000000 -i 1620 -b 1536 -g AGC -l 3 -f 162550000 -x 300
```

 - same as the first example, but streaming for one hour and writing to a SDRuno-compatible file with time markers at 0, 5, 10, 15, ..., 50, 55 minutes after the hour:
```
rsp-recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000 -x 3600 -t SDRuno
```

 - same as the first example, streaming for one hour and writing to an experimental format file with time markers at 0, 5, 10, 15, ..., 50, 55 minutes after the hour:
```
rsp-recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000 -x 3600 -t experimental -m 300
```

 - same as the the SDRuno example above, but with AGC enabled and storing the gain values in a `.gains` file:
```
rsp-recorder -r 6000000 -i 1620 -b 1536 -g AGC -l 3 -f 162550000 -x 3600 -t SDRuno -G
```

 - RDPdx recording using the configuration file above (`rspdx-hdr.conf`) for 10 minutes:
```
rsp-recorder -c rspdx-hdr.conf -x 600
```

### RSPduo in single tuner mode

Most of the examples above also work with the RSPduo in single tuner mode.
To select single tuner mode for the RSPduo use '-w 1' (or 'RSPduo mode = 1' in the config file).

### RSPduo in master mode

Most of the examples above also work with the RSPduo in master mode.
To select master mode for the RSPduo use '-w 4' (or 'RSPduo mode = 4' in the config file).

### RSPduo in slave mode

For this mode you should already have another SDR application stream from the RSPduo in master mode.
In this case rsp-recorder will automatically select slave mode.

### RSPduo in dual tuner mode

Most of the examples above also work with the RSPduo in dual tuner mode.
This mode is the default for the RSPduo when the right combination of sample rate, IF frequency, and IF bandwidth is selected (see table above).

 - MW dual tuner recording using the configuration file above (`mw-dual-tuner.conf`) for 10 minutes:
```
rsp-recorder -c mw-dual-tuner.conf -x 600
```


## References

  - SDRplay API Specification (version 3.15): https://www.sdrplay.com/docs/SDRplay_API_Specification_v3.15.pdf
  - SDRplay RSPduo Technical Information: https://www.sdrplay.com/wp-content/uploads/2018/06/RSPDuo-Technical-Information-R1P1.pdf
  - Linrad: https://www.sm5bsz.com/linuxdsp/linrad.htm
  - EBU Technical Specification 3306 - MBWF/RF64: An extended File Format for Audio - version 1.1, July 2009: https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf


## Copyright

(C) 2025 Franco Venturi - Licensed under the GNU GPL V3 (see [LICENSE](LICENSE))
