> [!IMPORTANT]
> **Driver Installation**: To correctly install device Drivers, please install the original application for the board before using the application. Releases can be found [here](https://github.com/espotek-org/Labrador/releases). Future releases may not require this step.

# About

- This application is a control interface for the [Espotek Labrador Portable Lab Bench](https://espotek.com/labrador/)
- The primary goals of the design it to be student friendly and accessible, meaning that advanced features are not present
- The application was developed as an ECSE Final Year Project at Monash University
<p align="center">
  <img src="https://github.com/user-attachments/assets/72207e83-18da-40ee-9f17-82211c3f7350" alt="labrador-controller-home-page" width="750"/>
</p>

# Installation

### Windows
- v1.0.0 Release (October 2024) found [here](https://github.com/jdar0011/Labrador_ImGui/releases/tag/v.1.0.0)
- Download and run .msi file, application and relevant driver will be installed.

### macOS
- Beta Release (October 2025) found [here](https://github.com/jdar0011/Labrador_ImGui/releases/tag/Beta)
- Download and open .dmg file, code is currently **unsigned** so macOS makes it difficult to open
- After dragging the LabradorImgui.app into the Application folder, run this line of code to allow the app to open (works on macOS Sequoia V15.6)
```
sudo xattr -cr /Applications/LabradorImgui.app
```

# Running the Code

### Windows
- This application is developed with Visual Studio 2022
- Clone the respository using Visual Studio 2022 to build and run the latest variant of the application

### macOS
- Download cmake (if you have homebrew (https://brew.sh/) the command is:)
```
brew install cmake
```
- Run ./package_mac.sh file: (this just calls all the cmake commands and constructs the App bundle.)
```
chmod +x package_mac.sh
./package_mac.sh
```
- LabraScope executable can be run via
```
./mac_appbundle/LabraScope.app/Contents/MacOS/LabraScope
```
- or you can open LabraScope.app from the mac_appbundle folder

# User Documentation

### General

#### About the board

- This application controls the EspoTek Labrador Board.
- Not all pins are utilised by this application. See Pinout below for a diagram showing the location of all pins used by this application.
- The pins that are labelled 'AC' (short for AC Coupled) will contain, perhaps unexpectedly for the user, a DC component of ~1.65 V. The board manufacturer highly recommends that DC Pins are used unless the user is certain of what they are doing.

### Troubleshooting

#### Safety Mode

- Safety Mode is indicated by the Labrador Board's LED flashing every 400 ms
- This will align with short blips on the Oscilloscope reading surrounded by null readings
- Fix this by unplugging the board for 30 seconds before plugging it back in

### Power Supply Unit (PSU)

#### Controls
- **Voltage control**: Adjusts the Power Supply output voltage level

#### Specifications
- **Voltage Range**: 4.5 V - 12.0 V
- **Maximum Power Supply**: 0.75 W

### Signal Generator

#### Controls
- **Power button**: Turns the signal generator on or off.
- **Waveform selection**: Chooses the type of waveform output.
- **Frequency slider**: Sets the frequency of the signal.
- **Vpeak-peak slider**: Modifies the peak to peak amplitude of the waveform.
- **Vbase slider**: Shifts the waveform's base voltage level.
- **Phase slider**: Shifts the waveform in the time domain. For example, 90deg phase will move the waveform 'forward' by a quarter of the waveform's period.
- **Duty Cycle slider**: Defines the percentage that the waveform is "on" (square wave only)

#### Specifications
- **Voltage Range**: 
  - 0.15 V to 9.5 V (DC Coupled)
  - -4.5 V to 4.5 V (AC Coupled)
- **Maximum Current**: 10 mA
- **Sample Rate**: 1 Msps

#### Additional Information
- **DC Coupled Pins**: Replicates the properties set in the application.
- **AC Coupled Pins**: Constant ~1.65 V DC component meaning the Vbase set in the application may not have the desired effect.
- Note: The traditional definition of an AC Coupled Signal is a signal that has the DC component filtered out. This is NOT the case with the Labrador hardware - the signal generator will produce a 1.65 V DC component on 'AC' Pins. Use with caution as this may be unexpected for some users.
- Note: Users that are experienced with electronics may expect the Signal Generator Controls to offer parameters more commonly found on traditional signal generator equipment, such as a DC offset controlling the DC component and an amplitude controlling the maximum extent of a wave measured from this DC component. The controls provided are influenced by the capabilities of the Labrador board. The board cannot drive negative voltages, therefore the DC component can never be zero for non-zero amplitude waveforms - this would greatly complicate the behaviour of the controls if they were to be mirror traditional signal generators.

### Plot Settings

#### Main Controls

- **Run / Stop**: Starts or stops the signal acquisition and display.
- **Auto Fits**: Sets zoom of plot to appropriate scale, based on calculated period for time axis or Vpp for voltage axis.

#### Display

- **Channel Switches**: Enables/disables respective channel reading and display.
- **Cursors**: Enables/disables measurement cursors. Both cursors must be active to display delta information under plot window.
- **Signal Properties**: Enables/disables signal property calculations displayed under plot window.

#### General Settings

- **Trigger**:
  - Aligns a 'trigger' event with the left most point in the plot window (e.g. OSC1 Rising)
  - **Level**: Sets the voltage level for triggering
- **Export**
  - To clipboard: Copies current data in time window to clipboard, can be easily pasted into Excel.
  - To CSV: Saves current data in time window to selected directory. Press Export button to choose directory.

#### Specifications

- **Sample Rate**: 750 ksps
- **Input Voltage Range**: -20 V to +20 V

#### Additional Information

- DC Coupled pins will read the raw voltages without filtering anything.
- AC Coupled pins filter out the very low frequency components (DC components) to display only AC signals

#### Math Mode

- Combine channels mathematically. eg. osc1 + osc2

- **Syntax**
- **Literals and Names**
  - Signals: osc1   osc2   t   
  - Numbers: 3    -4.2    1e-3
  - Constants: pi   e
- **Operators**
  - Arithmetic: +   -   *   /   %   ^
  - Comparison: ==    !=    <   <=    >   >=
  - Logical: and    or    xor   not
- **Common Functions**
  - abs(x)    sgn(x)
  - floor(x)    ceil(x)   round(x)    trunc(x)    frac(x)
  - sqrt(x)   pow(x,y)
  - exp(x)    log(x)    log10(x)
  - sin(x)    cos(x)    tan(x)
  - asin(x)   acos(x)   atan(x)
  - sinh(x)   cosh(x)   tanh(x)

### Analysis Tools

#### General

- Analysis tools are displayed in a secondary plot underneath the oscilloscope plot window. 
- Toggle ON/OFF via the switch. 
- Select the tool via the tabs under the switch. Only the active tool is displayed.

#### Spectrum Analyser

- **Start Acquiring/Acquire**: Acquire oscilloscope data to pass through FFT algorithm. Results are displayed after data acquisition has been completed. Behaviour depends on "Acquisition Mode" (gated or lookback, see description for details).
- **Auto Fit**: Automatically set the plot scale to best show the data.
- **Display**: Toggle the display of either oscilloscope channel. 
- **Advanced Options**:
  - **Sample Rate**: How many times per second the oscilloscope is sampled.
  - **Max Time Window/Time Window**: Max duration of acquisition., or duration of acquisition, depending on acquisition mode (see description for details)
  - **Windowing Function**: Shape applied to the signal before the FFT. Helps reduce leakage and improve peak clarity.
    - **Rectangular**: No tapering. Highest frequency resolution, but more spectral leakage.
    - **Hann**: Smooth tapering. Lower leakage and cleaner peaks, slightly reduced resolution.
  - **Vertical Units**: Scale used to display FFT magnitude.
    - **dBm**: Power level referenced to 1 mW (assumes 50 Ω load). Displayed on a logarithmic dB scale.
    - **dBV**: Voltage level referenced to 1 V (20 log10(V)). Displayed on a logarithmic dB scale.
    - **V RMS**: Root-mean-square voltage of each frequency component. Direct physical voltage measurement on a linear scale.
  - **Acquisition Mode**: Choose the desired mode of data acquisition.
    - **Gated (Start-Stop)**: Starts acquiring the moment the "Start Acquiring" button is pressed. Stops acquiring after a time segment equal to "Max Time Window" has passed, or the button is pressed again.
    - **Lookback**: Immediately acquires the most recent time segment equal to the "Time Window" (i.e., the last *t* seconds), without waiting for new data.

#### Network Analyser

- **Acquire**: Commence frequency sweep.
- **Auto Fit**: Automatically set the plot scale to best show the data.
- **Phase**: Display the phase plot.
- **Stimulus Generator**: Signal Generator that is the input for the network.
- **Reference (Input) Channel**: Oscilloscope channel reading the network input.
- **Reference (Output) Channel**: Oscilloscope channel reading the network output.
- **Frequency Range**: Select range for frequency sweep.
- **Advanced Options**:
  - **Vertical Units**: Scale used to display frequency response magntiude.
    - **dB**: Displays the frequency response magnitude using a logarithmic decibel scale (20 log10). Highlights relative gain and attenuation more clearly.
    - **Linear**: Displays the raw magnitude of the frequency response on a linear, unitless scale.
  - **Number of Data Points**: Number of differenct frequencies to test in the frequency sweep.
  - **Point Spacing**: 
    - **Logarithmic**: higher freqency inputs are more spaced apart. 
    - **Linear**: evenly spaced frequncy inputs.

### Plot Window

#### Controls

- **Panning**:
  - Click and drag on the plot window to pan in any direction
  - Click and drag on an axis to pan in only that direction
- **Zooming**: 
  - Scroll to zoom anywhere on the plot
  - Scroll on an axis to zoom only on that axis

#### Measurments

- **Cursors**:
  - Activate cursors in Plot Settings
  - Measurements between the two cursors are displayed below the plot when they are both activated
  - Use these to measure the frequency/amplitude of displayed waveforms
- **Signal Properties**
  - Signal properties are calculated based on the plot window data
  - ∆T values represent an average time between trigger events (see more info in Plot Settings)
  - ∆V values represent the difference between Vmin and Vmax
### End
