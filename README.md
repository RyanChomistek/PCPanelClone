To install go to releases and download and run the installer

once installed it should automatically start. it will auto generate a file called MixerSettings.json in your documents folder
 - in the file there is an array of dials, the index in the array corresponds to the index of the dial
 - for each dial there are three properties, Color, Target, and Processes
   - Color: an array that has 3 ints representing the color of the dial led
   - Target what the dial does. currently there are three states
     - 0: target processes (there should be annother property named Processes on the object). This means that the mixer will set the volume of all of the listed processes
     - 1: Master, sets the master volume
     - 2: Device, sets the volume of a specific output device (not sure if this one works yet)
     - 3: focused, set the volume of the currently focused window
   - Processes: an array of process strings. if Target is set to 3, then it will use this list to find the processes it wants to change
