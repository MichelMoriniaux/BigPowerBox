//
// ASCOM Switch hardware class for ShortCircuitBigPowerSwitch
//
// Description:	 Hardware driver for the Big Power Box Exxxtreme
//
// Implements:	ASCOM Switch interface version: 0.1
// Author:		(2023) Michel Moriniaux <first.last@gmail.com>
//

using ASCOM;
using ASCOM.Astrometry;
using ASCOM.Astrometry.NOVAS;
using ASCOM.DeviceInterface;
using ASCOM.LocalServer;
using ASCOM.Utilities;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace ASCOM.ShortCircuitBigPowerSwitch.Switch
{
    /// <summary>
    /// ASCOM Switch hardware class for ShortCircuitBigPowerSwitch.
    /// </summary>
    [HardwareClass()] // Class attribute flag this as a device hardware class that needs to be disposed by the local server when it exits.
    internal static class SwitchHardware
    {
        // Constants used for Profile persistence
        internal const string comPortProfileName = "COM Port";
        internal const string comPortDefault = "COM1";
        internal const string traceStateProfileName = "Trace Level";
        internal const string traceStateDefault = "false";

        private static string DriverProgId = ""; // ASCOM DeviceID (COM ProgID) for this driver, the value is set by the driver's class initialiser.
        private static string DriverDescription = ""; // The value is set by the driver's class initialiser.
        internal static string comPort; // COM port name (if required)
        private static bool connectedState; // Local server's connected state
        private static bool workerCanRun; // the worker thread that updates the port status can run
        private static bool runOnce = false; // Flag to enable "one-off" activities only to run once.
        internal static Util utilities; // ASCOM Utilities object for use as required
        internal static TraceLogger tl; // Local server's trace logger object for diagnostic log with information that you specify

        /// <summary>
        /// Serial interface to communicate with the device
        /// </summary>
        private static Serial objSerial;  // the Serial instance to interact with the comm port
        private static int serialConnectionCount = 0;     // counter for the number of clients connected to the server
        private static readonly object lockObject = new Object();   // Object Locker for thread safety

        /// <summary>
        ///  some basic commands to interact with the switch
        /// </summary>
        private const string SOC = ">";                 // Start of Command marker
        private const string EOC = "#";                 // End of Command marker
        private const string PINGCOMMAND = ">P#";       // ping command
        private const string PINGREPLY = ">POK#";       // ping reply
        private const string GETSTATUS = ">S#";         // status request command
        private const string GETDESCRIPTION = ">D#";    // board description request command
        private static string BoardSignature;           // string to store the board geometry
        private static string deviceName;               // the device name stored on the board
        private static string hwRevision;               // the HW revision sotred on the board
        private const short SWH = 0;                    // switched port type
        private const short MPX = 1;                    // Multiplexed port type
        private const short PWM = 2;                    // PWM port type
        private const short AON = 3;                    // Allways-On port type
        private const short CURRENT = 4;                // output Current port type (sensor)
        private const short INPUTA = 5;                 // Input Current port type (sensor)
        private const short INPUTV = 6;                 // Input Voltage port type (sensor)
        private const short TEMP = 7;                   // Temperature port type (sensor)
        private const short HUMID = 8;                  // Humidity port type (sensor)
        private const short DEWPOINT = 9;               // Dewpoint (sensor)
        private const short MODE = 10;                  // PWM port mode switch
        private const short SETTEMP = 11;               // PWM port temperature offset switch
        private const int UPDATEINTERVAL = 2000;        // how often to update the status

        class Feature_c
        {
            public bool canWrite;
            public bool state;
            public short type;         // SWH, MPX, PWM, AON
            public int port;           // port number
            public double value;
            public double minvalue;
            public double maxvalue;
            public string unit;
            public string description;
            public string name;
        };                                              // internal status of a port class
        private static List<Feature_c> deviceFeatures;  // list of ports to store their status
        private static int portNum;                     // number of physical electrical ports
        private static bool havePWM = false;            // do we have PWM ports in the port list
        public class fakePort_c
        {
            public int index { get; set; }
            public string Name { get; set; }
            public fakePort_c(int i, string n)
            {
                this.index = i;
                this.Name = n;
            }
        }                                               // class to store port names to edit them in the setup dialog
        internal static bool updateNames = false;       // flag to update the names post config dialog
        internal static List<fakePort_c> configPortNames;
        private static Thread workerThread;             // worker thread that polls the board with GETSTATUS every UPDATEINTERVAL

        /// <summary>
        /// Initializes a new instance of the device Hardware class.
        /// </summary>
        static SwitchHardware()
        {
            try
            {
                // Create the hardware trace logger in the static initialiser.
                // All other initialisation should go in the InitialiseHardware method.
                tl = new TraceLogger("", "ShortCircuitBigPowerSwitch.Hardware");

                // DriverProgId has to be set here because it used by ReadProfile to get the TraceState flag.
                DriverProgId = Switch.DriverProgId; // Get this device's ProgID so that it can be used to read the Profile configuration values

                // ReadProfile has to go here before anything is written to the log because it loads the TraceLogger enable / disable state.
                ReadProfile();

                LogMessage("SH.SwitchHardware", $"Static initialiser completed.");
            }
            catch (Exception ex)
            {
                try { LogMessage("SH.SwitchHardware", $"Initialisation exception: {ex}"); } catch { }
                MessageBox.Show($"{ex.Message}", "Exception creating ASCOM.ShortCircuitBigPowerSwitch.Switch", MessageBoxButtons.OK, MessageBoxIcon.Error);
                throw;
            }
        }

        /// <summary>
        /// Place device initialisation code here
        /// </summary>
        /// <remarks>Called every time a new instance of the driver is created.</remarks>
        internal static void InitialiseHardware()
        {
            // This method will be called every time a new ASCOM client loads your driver
            LogMessage("SH.InitialiseHardware", $"Start.");

            // Make sure that "one off" activities are only undertaken once
            if (runOnce == false)
            {
                LogMessage("SH.InitialiseHardware", $"Starting one-off initialisation.");

                DriverDescription = Switch.DriverDescription; // Get this device's Chooser description

                LogMessage("SH.InitialiseHardware", $"ProgID: {DriverProgId}, Description: {DriverDescription}");

                connectedState = false; // Initialise connected to false
                utilities = new Util(); // Initialise ASCOM Utilities object this could be removed but is left here for dewpoint calculations in the future

                LogMessage("SH.InitialiseHardware", "Completed basic initialisation");

                // Add your own "one off" device initialisation here e.g. validating existence of hardware and setting up communications
                deviceFeatures = new List<Feature_c>();
                configPortNames = new List<fakePort_c>();
                // starting status updater thread
                tl.LogMessage("SH.InitialiseHardware", "update status thread start");
                workerCanRun = false;   // we do not want the worker to poll at this stage as we are not yet connected to Serial
                workerThread = new Thread(new ThreadStart(updateStatus));
                workerThread.Start();

                LogMessage("SH.InitialiseHardware", $"One-off initialisation complete.");
                runOnce = true; // Set the flag to ensure that this code is not run again
            }
        }

        // PUBLIC COM INTERFACE ISwitchV2 IMPLEMENTATION

        #region Common properties and methods.

        /// <summary>
        /// Displays the Setup Dialogue form.
        /// If the user clicks the OK button to dismiss the form, then
        /// the new settings are saved, otherwise the old values are reloaded.
        /// THIS IS THE ONLY PLACE WHERE SHOWING USER INTERFACE IS ALLOWED!
        /// </summary>
        public static void SetupDialog()
        {
            configPortNames.Clear();
            // if we have never connected then deviceFeatures is empty, so we populate the configPortNames with fake generic names
            if (deviceFeatures.Count == 0)
            {
                for (short i = 1; i <= 14; i++)
                {
                    configPortNames.Add(new fakePort_c(i, $"Port {i}"));
                }
            }
            else
            {
                // deviceFeatures has been populated so we can list the real port names in the config dialog
                for (short i = 1; i <= portNum; i++)
                {
                    configPortNames.Add(new fakePort_c(i, deviceFeatures[i - 1].name));
                }
            }
            tl.LogMessage("SH.SetupDialog", "Completed initialisation");

            using (SetupDialogForm F = new SetupDialogForm(tl))
            {
                var result = F.ShowDialog();
                if (result == DialogResult.OK)
                {
                    // Persist device configuration values to the ASCOM Profile store
                    WriteProfile();
                    // if we have modified any of the stored names then we need to update the board with the new names
                    if (deviceFeatures.Count != 0 && updateNames)
                    {
                        CopyNamesToDevice();
                        updateNames = false;
                    }
                }
            }
        }

        /// <summary>Returns the list of custom action names supported by this driver.</summary>
        /// <value>An ArrayList of strings (SafeArray collection) containing the names of supported actions.</value>
        public static ArrayList SupportedActions
        {
            get
            {
                LogMessage("SH.SupportedActions Get", "Returning empty ArrayList");
                return new ArrayList();
            }
        }

        /// <summary>Invokes the specified device-specific custom action.</summary>
        /// <param name="ActionName">A well known name agreed by interested parties that represents the action to be carried out.</param>
        /// <param name="ActionParameters">List of required parameters or an <see cref="String.Empty">Empty String</see> if none are required.</param>
        /// <returns>A string response. The meaning of returned strings is set by the driver author.
        /// <para>Suppose filter wheels start to appear with automatic wheel changers; new actions could be <c>QueryWheels</c> and <c>SelectWheel</c>. The former returning a formatted list
        /// of wheel names and the second taking a wheel name and making the change, returning appropriate values to indicate success or failure.</para>
        /// </returns>
        public static string Action(string actionName, string actionParameters)
        {
            LogMessage("SH.Action", $"Action {actionName}, parameters {actionParameters} is not implemented");
            throw new ActionNotImplementedException("Action " + actionName + " is not implemented by this driver");
        }

        /// <summary>
        /// Transmits an arbitrary string to the device and does not wait for a response.
        /// Optionally, protocol framing characters may be added to the string before transmission.
        /// </summary>
        /// <param name="Command">The literal command string to be transmitted.</param>
        /// <param name="Raw">
        /// if set to <c>true</c> the string is transmitted 'as-is'.
        /// If set to <c>false</c> then protocol framing characters may be added prior to transmission.
        /// </param>
        public static void CommandBlind(string command, bool raw)
        {
            CheckConnected("SwitchHardware.CommandBlind");

            throw new MethodNotImplementedException($"CommandBlind - Command:{command}, Raw: {raw}.");
        }

        /// <summary>
        /// Transmits an arbitrary string to the device and waits for a boolean response.
        /// Optionally, protocol framing characters may be added to the string before transmission.
        /// </summary>
        /// <param name="Command">The literal command string to be transmitted.</param>
        /// <param name="Raw">
        /// if set to <c>true</c> the string is transmitted 'as-is'.
        /// If set to <c>false</c> then protocol framing characters may be added prior to transmission.
        /// </param>
        /// <returns>
        /// Returns the interpreted boolean response received from the device.
        /// </returns>
        public static bool CommandBool(string command, bool raw)
        {
            CheckConnected("CommandBool");

            throw new MethodNotImplementedException($"CommandBool - Command:{command}, Raw: {raw}.");
        }

        /// <summary>
        /// Transmits an arbitrary string to the device and waits for a string response.
        /// Optionally, protocol framing characters may be added to the string before transmission.
        /// </summary>
        /// <param name="Command">The literal command string to be transmitted.</param>
        /// <param name="Raw">
        /// if set to <c>true</c> the string is transmitted 'as-is'.
        /// If set to <c>false</c> then protocol framing characters may be added prior to transmission.
        /// </param>
        /// <returns>
        /// Returns the string response received from the device.
        /// </returns>
        public static string CommandString(string command, bool raw)
        {
            lock (lockObject)
            {
                CheckConnected("CommandString");
                tl.LogMessage("SH.CommandString", "Send Command" + command);
                objSerial.Transmit(command);
                tl.LogMessage("SH.CommandString", "Waiting for response from device...");
                string response;
                try
                {
                    response = objSerial.ReceiveTerminated(EOC).Trim();
                }
                catch (Exception e)
                {
                    tl.LogMessage("SH.CommandString", "Exception: " + e.Message + " for command " + command);
                    throw e;
                }
                tl.LogMessage("SH.CommandString", "Response from device: " + response);
                // cleanup SoC and EoC
                response = response.Replace(EOC, string.Empty);
                response = response.Replace(SOC, string.Empty);

                return response;
            }
        }

        /// <summary>
        /// Deterministically release both managed and unmanaged resources that are used by this class.
        /// </summary>
        /// <remarks>
        /// 
        /// Do not call this method from the Dispose method in your driver class.
        ///
        /// This is because this hardware class is decorated with the <see cref="HardwareClassAttribute"/> attribute and this Dispose() method will be called 
        /// automatically by the  local server executable when it is irretrievably shutting down. This gives you the opportunity to release managed and unmanaged 
        /// resources in a timely fashion and avoid any time delay between local server close down and garbage collection by the .NET runtime.
        ///
        /// For the same reason, do not call the SharedResources.Dispose() method from this method. Any resources used in the static shared resources class
        /// itself should be released in the SharedResources.Dispose() method as usual. The SharedResources.Dispose() method will be called automatically 
        /// by the local server just before it shuts down.
        /// 
        /// </remarks>
        public static void Dispose()
        {
            try { LogMessage("SH.Dispose", $"Disposing of assets and closing down."); } catch { }
            try
            {
                workerThread.Abort();
                workerThread = null;
            }
            catch { }

            try
            {
                // Clean up the trace logger and utility objects
                tl.Enabled = false;
                tl.Dispose();
                tl = null;
            }
            catch { }

            try
            {
                utilities.Dispose();
                utilities = null;
            }
            catch { }
        }

        /// <summary>
        /// Set True to connect to the device hardware. Set False to disconnect from the device hardware.
        /// You can also read the property to check whether it is connected. This reports the current hardware state.
        /// </summary>
        /// <value><c>true</c> if connected to the hardware; otherwise, <c>false</c>.</value>
        public static bool Connected
        {
            get
            {
                LogMessage("SH.Connected", $"Get {IsConnected}");
                return IsConnected;
            }
            set
            {
                lock (lockObject)
                {
                    // return is we are requesting the status we are already in
                    // this usually happens when an additional client driver wants to dis/connect 
                    LogMessage("SH.Connected", "Set {0}", value);
                    if (value == IsConnected)
                    {
                        if (value)
                            serialConnectionCount++;
                        else
                            serialConnectionCount--;
                        LogMessage("SH.Connected", "Serial connection count: {0}", serialConnectionCount);
                        return;
                    }

                    // connect / disconnect
                    if (value)
                    {
                        // we want to connect
                        if (serialConnectionCount == 0)
                        {
                            // there are no current connections and we were never initialized so lets go through full initialization
                            LogMessage("SH.Connected", "Doing initial Initialization");
                            if (comPort == null)
                            {
                                comPort = comPortDefault;
                            }

                            if (!System.IO.Ports.SerialPort.GetPortNames().Contains(comPort))
                            {
                                throw new InvalidValueException("Invalid COM port", comPort.ToString(), String.Join(", ", System.IO.Ports.SerialPort.GetPortNames()));
                            }

                            objSerial = new Serial
                            {
                                Speed = SerialSpeed.ps9600,
                                PortName = comPort,
                                Connected = true
                            };

                            // Wait a second for the serial connection to establish
                            System.Threading.Thread.Sleep(1000);

                            objSerial.ClearBuffers();

                            // Poll the device (with a short timeout value) until successful,
                            // or until we've reached the retry count limit of 3...
                            objSerial.ReceiveTimeout = 1;
                            bool success = false;
                            for (int retries = 3; retries >= 0; retries--)
                            {
                                string response = "";
                                try
                                {
                                    objSerial.Transmit(PINGCOMMAND);
                                    response = objSerial.ReceiveTerminated(EOC).Trim();
                                }
                                catch (Exception)
                                {
                                    // ignore any errors here
                                }
                                if (response == PINGREPLY)
                                {
                                    success = true;
                                    break;
                                }
                            }

                            if (!success)
                            {
                                objSerial.Connected = false;
                                objSerial.Dispose();
                                objSerial = null;
                                throw new ASCOM.NotConnectedException("Failed to connect");
                            }

                            // we are now connected
                            connectedState = true;
                            LogMessage("SH.Connected Set", "Connected to port {0}", comPort);

                            // Restore default timeout value...
                            objSerial.ReceiveTimeout = 10;

                            // now that we are connected lets configure the driver with the devices ports
                            tl.LogMessage("SH.Connected", "Query device signature");
                            QueryDeviceDescription();
                            // lets populate the Status struct
                            tl.LogMessage("SH.Connected", "Query device status");
                            QueryDeviceStatus();
                            // populate the port names
                            tl.LogMessage("SH.Connected", "Query Port Names");
                            QueryPortNames();
                            // Query the PWM ports for their options (mode, value, temp offset) also modify the port type if the PWM port is switchable
                            if (havePWM)
                            {
                                tl.LogMessage("SH.Connected", "Query PWM Ports");
                                QueryPWMPorts();
                            }
                            // we are now initialized
                            tl.LogMessage("SH.Connected", "Initialization done");

                            // now that all datastructures have been populated lets allow the worker to run and poll the board
                            workerCanRun = true;
                            // wake up the worker thread
                            tl.LogMessage("SH.Connected", "wake up Worker Thread");
                            workerThread.Interrupt();
                        }
                        serialConnectionCount++;
                    }
                    else
                    {
                        // disconnect
                        serialConnectionCount--;
                        if (serialConnectionCount <= 0)
                        {
                            connectedState = false;
                            workerCanRun = false;
                            // LogMessage("Connected Set", "Stoping worker Thread");
                            // workerThread.Join();
                            LogMessage("SH.Connected Set", "Disconnecting from port {0}", comPort);
                            objSerial.Connected = false;
                            objSerial.Dispose();
                            objSerial = null;
                        }
                    }
                    LogMessage("SH.Connected", "Serial connection count: {0}", serialConnectionCount);
                }
            }
        }

        /// <summary>
        /// Returns a description of the device, such as manufacturer and model number. Any ASCII characters may be used.
        /// </summary>
        /// <value>The description.</value>
        public static string Description
        {
            get
            {
                LogMessage("SH.Description Get", DriverDescription);
                return DriverDescription;
            }
        }

        /// <summary>
        /// Descriptive and version information about this ASCOM driver.
        /// </summary>
        public static string DriverInfo
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                string driverInfo = $"Information about the driver itself. Version: {version.Major}.{version.Minor}";
                LogMessage("SH.DriverInfo Get", driverInfo);
                return driverInfo;
            }
        }

        /// <summary>
        /// A string containing only the major and minor version of the driver formatted as 'm.n'.
        /// </summary>
        public static string DriverVersion
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                string driverVersion = $"{version.Major}.{version.Minor}";
                LogMessage("SH.DriverVersion Get", driverVersion);
                return driverVersion;
            }
        }

        /// <summary>
        /// The interface version number that this device supports.
        /// </summary>
        public static short InterfaceVersion
        {
            // set by the driver wizard
            get
            {
                LogMessage("SH.InterfaceVersion Get", "2");
                return Convert.ToInt16("2");
            }
        }

        /// <summary>
        /// The short name of the driver, for display purposes
        /// </summary>
        public static string Name
        {
            get
            {
                LogMessage("SH.Name Get", DriverDescription);
                return $"{DriverDescription} - {deviceName} - rev. {hwRevision}";
            }
        }

        #endregion

        #region ISwitchV2 Implementation

        private static short numSwitch = 0;

        /// <summary>
        /// The number of switches managed by this driver
        /// </summary>
        /// <returns>The number of devices managed by this driver.</returns>
        internal static short MaxSwitch
        {
            get
            {
                LogMessage("SH.MaxSwitch Get", numSwitch.ToString());
                return numSwitch;
            }
        }

        /// <summary>
        /// Return the name of switch device n.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The name of the device</returns>
        internal static string GetSwitchName(short id)
        {
            // this method is called by clients like N.i.n.a. every 2s for each port
            Validate("SH.GetSwitchName", id);
            tl.LogMessage("SH.GetSwitchName", $"GetSwitchName({id}) - {deviceFeatures[id].name}");
            return deviceFeatures[id].name;
        }

        /// <summary>
        /// Set a switch device name to a specified value.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="name">The name of the device</param>
        internal static void SetSwitchName(short id, string name)
        {
            Validate("SH.SetSwitchName", id);
            // ">M:%02d:%s#" return ">MOK#"
            // EEPROM dies quick, lets not update it uselessly
            if (deviceFeatures[id].name == name)
            {
                tl.LogMessage("SH.SetSwitchName", $"SetSwitchName({id}) = {name} not modified");
                return;
            }
            if (id < portNum)
                CommandString(string.Format(">M:{0, 0:D2}:{1}#", id, name), false);
            deviceFeatures[id].name = name;
            deviceFeatures[id + portNum].name = name + " Current (A)";
            tl.LogMessage("SH.SetSwitchName", $"SetSwitchName({id}) = {name}");
        }

        /// <summary>
        /// Gets the description of the specified switch device. This is to allow a fuller description of
        /// the device to be returned, for example for a tool tip.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>
        /// String giving the device description.
        /// </returns>
        internal static string GetSwitchDescription(short id)
        {
            Validate("SH.GetSwitchDescription", id);
            tl.LogMessage("SH.GetSwitchDescription", $"GetSwitchDescription({id}) - {deviceFeatures[id].description}");
            return deviceFeatures[id].description;
        }

        /// <summary>
        /// Reports if the specified switch device can be written to, default true.
        /// This is false if the device cannot be written to, for example a limit switch or a sensor.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>
        /// <c>true</c> if the device can be written to, otherwise <c>false</c>.
        /// </returns>
        internal static bool CanWrite(short id)
        {
            Validate("SH.CanWrite", id);
            // default behavour is to report true
            tl.LogMessage("SH.CanWrite", $"CanWrite({id}): {deviceFeatures[id].canWrite}");
            return deviceFeatures[id].canWrite;
        }

        #region Boolean switch members

        /// <summary>
        /// Return the state of switch device id as a boolean
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>True or false</returns>
        internal static bool GetSwitch(short id)
        {
            Validate("SH.GetSwitch", id);
            tl.LogMessage("SH.GetSwitch", $"GetSwitch({id}) - {deviceFeatures[id].state}");
            return deviceFeatures[id].state;
        }

        /// <summary>
        /// Sets a switch controller device to the specified state, true or false.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="state">The required control state</param>
        internal static void SetSwitch(short id, bool state)
        {
            string command;
            Validate("SH.SetSwitch", id);
            if (!CanWrite(id))
            {
                var str = $"SetSwitch({id}) - Cannot Write";
                tl.LogMessage("SH.SetSwitch", str);
                throw new MethodNotImplementedException(str);
            }
            deviceFeatures[id].state = state;
            if (state)
            {
                if (deviceFeatures[id].type == PWM)
                    command = string.Format(">W:{0, 0:D2}:255#", id);
                else
                    command = string.Format(">O:{0, 0:D2}#", id);
            }
            else
            {
                if (deviceFeatures[id].type == PWM)
                    command = string.Format(">W:{0, 0:D2}:0#", id);
                else
                    command = string.Format(">F:{0, 0:D2}#", id);
            }
            // Make it so!
            CommandString(command, false);
            tl.LogMessage("SH.SetSwitch", $"SetSwitch({id}) = {state} - {command}");
        }

        #endregion

        #region Analogue members

        /// <summary>
        /// Returns the maximum value for this switch device, this must be greater than <see cref="MinSwitchValue"/>.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The maximum value to which this device can be set or which a read only sensor will return.</returns>
        internal static double MaxSwitchValue(short id)
        {
            Validate("SH.MaxSwitchValue", id);
            tl.LogMessage("SH.MaxSwitchValue", $"MaxSwitchValue({id}) - {deviceFeatures[id].maxvalue}");
            return deviceFeatures[id].maxvalue;
        }

        /// <summary>
        /// Returns the minimum value for this switch device, this must be less than <see cref="MaxSwitchValue"/>
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The minimum value to which this device can be set or which a read only sensor will return.</returns>
        internal static double MinSwitchValue(short id)
        {
            Validate("SH.MinSwitchValue", id);
            tl.LogMessage("SH.MinSwitchValue", $"MinSwitchValue({id}) - {deviceFeatures[id].minvalue}");
            return deviceFeatures[id].minvalue;
        }

        /// <summary>
        /// Returns the step size that this device supports (the difference between successive values of the device).
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The step size for this device.</returns>
        internal static double SwitchStep(short id)
        {
            Validate("SH.SwitchStep", id);
            tl.LogMessage("SH.SwitchStep", $"SwitchStep({id}) - 1.0");
            return 1.0;
        }

        /// <summary>
        /// Returns the value for switch device id as a double
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The value for this switch, this is expected to be between <see cref="MinSwitchValue"/> and
        /// <see cref="MaxSwitchValue"/>.</returns>
        internal static double GetSwitchValue(short id)
        {
            Validate("SH.GetSwitchValue", id);
            tl.LogMessage("SH.GetSwitchValue", $"GetSwitchValue({id}) - {deviceFeatures[id].value}");
            return deviceFeatures[id].value;
        }

        /// <summary>
        /// Set the value for this device as a double.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="value">The value to be set, between <see cref="MinSwitchValue"/> and <see cref="MaxSwitchValue"/></param>
        internal static void SetSwitchValue(short id, double value)
        {
            string command = "";
            tl.LogMessage("SH.SetSwitchValue", $"SetSwitchValue({id}) = {value}");
            Validate("SH.SetSwitchValue", id, value);
            if (!CanWrite(id))
            {
                tl.LogMessage("SH.SetSwitchValue", $"SetSwitchValue({id}) - Cannot write");
                throw new ASCOM.MethodNotImplementedException($"SetSwitchValue({id}) - Cannot write");
            }
            else
            {
                deviceFeatures[id].value = value;
                if (value > 0)
                {
                    switch (deviceFeatures[id].type)
                    {
                        case PWM:
                            command = string.Format(">W:{0, 0:D2}:{1}#", id, value);
                            break;
                        case MODE:
                            command = string.Format(">C:{0, 0:D2}:{1}#", deviceFeatures[id].port - 1, value);
                            // TODO: modify seviceFeatures[id - 1].type
                            if (value == 1)
                            {
                                deviceFeatures[deviceFeatures[id].port - 1].type = SWH;
                                deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 1;
                            }
                            else
                            {
                                if (deviceFeatures[deviceFeatures[id].port - 1].value == 1)
                                {
                                    deviceFeatures[deviceFeatures[id].port - 1].type = PWM;
                                    deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 255;
                                    deviceFeatures[deviceFeatures[id].port - 1].value = 255;
                                }
                            }
                            break;
                        case SETTEMP:
                            command = string.Format(">T:{0, 0:D2}:{1}#", deviceFeatures[id].port - 1, value);
                            break;
                        case SWH:
                        case MPX:
                        default:
                            command = string.Format(">O:{0, 0:D2}#", id);
                            break;
                    }
                }
                else
                {
                    switch (deviceFeatures[id].type)
                    {
                        case PWM:
                            command = string.Format(">W:{0, 0:D2}:0#", id);
                            break;
                        case MODE:
                            command = string.Format(">C:{0, 0:D2}:{1}#", deviceFeatures[id].port - 1, value);
                            deviceFeatures[deviceFeatures[id].port - 1].type = PWM;
                            deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 255;
                            break;
                        case SETTEMP:
                            command = string.Format(">T:{0, 0:D2}:{1}#", deviceFeatures[id].port - 1, value);
                            break;
                        case SWH:
                        case MPX:
                        default:
                            command = string.Format(">F:{0, 0:D2}#", id);
                            break;
                    }
                }
                // Make it so!
                CommandString(command, false);
                tl.LogMessage("SH.SetSwitchValue", $"SetSwitchValue({id}) = {value} - {command} done");

            }
        }

        #endregion

        #endregion

        #region Private methods

        /// <summary>
        /// Checks that the switch id is in range and throws an InvalidValueException if it isn't
        /// </summary>
        /// <param name="message">The message.</param>
        /// <param name="id">The id.</param>
        private static void Validate(string message, short id)
        {
            if (id < 0 || id >= numSwitch)
            {
                LogMessage(message, string.Format("Switch {0} not available, range is 0 to {1}", id, numSwitch - 1));
                throw new InvalidValueException(message, id.ToString(), string.Format("0 to {0}", numSwitch - 1));
            }
        }

        /// <summary>
        /// Checks that the switch id and value are in range and throws an
        /// InvalidValueException if they are not.
        /// </summary>
        /// <param name="message">The message.</param>
        /// <param name="id">The id.</param>
        /// <param name="value">The value.</param>
        private static void Validate(string message, short id, double value)
        {
            Validate(message, id);
            var min = MinSwitchValue(id);
            var max = MaxSwitchValue(id);
            if (value < min || value > max)
            {
                LogMessage(message, string.Format("Value {1} for Switch {0} is out of the allowed range {2} to {3}", id, value, min, max));
                throw new InvalidValueException(message, value.ToString(), string.Format("Switch({0}) range {1} to {2}", id, min, max));
            }
        }

        #endregion

        #region Private properties and methods
        // Useful methods that can be used as required to help with driver development

        /// <summary>
        /// Returns true if the worker thread can run
        /// </summary>
        private static bool UpdateCanRun
        {
            get
            {
                return workerCanRun;
            }
        }

        /// <summary>
        /// Returns true if there is a valid connection to the driver hardware
        /// </summary>
        private static bool IsConnected
        {
            get
            {
                return connectedState;
            }
        }

        /// <summary>
        /// Use this function to throw an exception if we aren't connected to the hardware
        /// </summary>
        /// <param name="message"></param>
        private static void CheckConnected(string message)
        {
            if (!IsConnected)
            {
                throw new NotConnectedException(message);
            }
        }

        /// <summary>
        /// Read the device configuration from the ASCOM Profile store
        /// </summary>
        internal static void ReadProfile()
        {
            using (Profile driverProfile = new Profile())
            {
                driverProfile.DeviceType = "Switch";
                tl.Enabled = Convert.ToBoolean(driverProfile.GetValue(DriverProgId, traceStateProfileName, string.Empty, traceStateDefault));
                comPort = driverProfile.GetValue(DriverProgId, comPortProfileName, string.Empty, comPortDefault);
            }
        }

        /// <summary>
        /// Write the device configuration to the  ASCOM  Profile store
        /// </summary>
        internal static void WriteProfile()
        {
            using (Profile driverProfile = new Profile())
            {
                driverProfile.DeviceType = "Switch";
                driverProfile.WriteValue(DriverProgId, traceStateProfileName, tl.Enabled.ToString());
                driverProfile.WriteValue(DriverProgId, comPortProfileName, comPort.ToString());
            }
        }

        /// <summary>
        /// Log helper function that takes identifier and message strings
        /// </summary>
        /// <param name="identifier"></param>
        /// <param name="message"></param>
        internal static void LogMessage(string identifier, string message)
        {
            tl.LogMessageCrLf(identifier, message);
        }

        /// <summary>
        /// Log helper function that takes formatted strings and arguments
        /// </summary>
        /// <param name="identifier"></param>
        /// <param name="message"></param>
        /// <param name="args"></param>
        internal static void LogMessage(string identifier, string message, params object[] args)
        {
            var msg = string.Format(message, args);
            LogMessage(identifier, msg);
        }

        /// <summary>
        /// Queries the device PWM ports and updates the driver's internal datastructures
        /// </summary>
        /// TODO: completely redo this method as it is not aligned with the rest
        private static void QueryPWMPorts()
        {
            string response = "";
            string[] words;
            for ( int i = 0; i < numSwitch; i++)
            {
                if (deviceFeatures[i].type == MODE)
                {
                    response = CommandString(string.Format(">G:{0, 0:D2}#", deviceFeatures[i].port - 1), false);
                    words = response.Split(':');
                    deviceFeatures[i].value = Convert.ToDouble(words[2]);
                    // if we get a mode 1 then the pwm switch is configured as an on/off switch
                    if (deviceFeatures[i].value == 1)
                    {
                        deviceFeatures[deviceFeatures[i].port - 1].type = SWH;
                        deviceFeatures[deviceFeatures[i].port - 1].maxvalue = 1;
                    }
                    deviceFeatures[i].state = true;
                    deviceFeatures[i].name = deviceFeatures[deviceFeatures[i].port - 1].name + " Mode";
                    tl.LogMessage("SH.QueryPWMPorts", "switch " + i + " mode " + deviceFeatures[i].value);
                }
                // temperature offset
                if (deviceFeatures[i].type == SETTEMP)
                {
                    response = CommandString(string.Format(">H:{0, 0:D2}#", deviceFeatures[i].port - 1), false);
                    words = response.Split(':');
                    deviceFeatures[i].value = Convert.ToDouble(words[2]);
                    deviceFeatures[i].state = true;
                    deviceFeatures[i].name = deviceFeatures[deviceFeatures[i].port - 1].name + " Temperature Offset";
                    tl.LogMessage("SH.QueryPWMPorts", "switch " + i + " offset " + deviceFeatures[i].value);
                }
            }

        }

        /// <summary>
        /// Queries the device for a status string and updates the driver's internal datastructures
        /// </summary>
        private static void QueryDeviceStatus()
        {
            CheckConnected("QueryDeviceStatus");

            // we do not want to query the status if we do not know the device's board signature so populate this first
            if (deviceFeatures.Count == 0)
            {
                QueryDeviceDescription();
            }
            tl.LogMessage("SH.QueryDeviceStatus", "Sending request to device...");
            string response = CommandString(GETSTATUS, false);
            tl.LogMessage("SH.QueryDeviceStatus", "Status string: " + response);
            //response should be like: 
            // S:0:0:0:0:0:0:0:0:0:0:0:0:8.87:7.19:6.29:5.96:5.89:5.94:5.94:5.94:5.91:5.84:5.82:5.77:0.00:0.00:0.08:3.61:0.00:0.00
            string[] words = response.Split(':');
            if (words.Length > 0)
            {
                if (words[0] != "S")
                {
                    tl.LogMessage("SH.QueryDeviceStatus", "Invalid response from device: " + response);
                    throw new ASCOM.DriverException("Invalid response from device: " + response);
                }
                else
                {
                    // populate the deviceFeatures List with the status values
                    string switchPortsOnly = BoardSignature.Replace("t", string.Empty);
                    switchPortsOnly = switchPortsOnly.Replace("f", string.Empty);
                    // first iterate through the ports to update the port values (OFF/ON/dutycycle level)
                    int index = 1;
                    for (int i = 0; i < switchPortsOnly.Length; i++)
                    {
                        if (switchPortsOnly[i] == 'm' || switchPortsOnly[i] == 's' || switchPortsOnly[i] == 'a')
                        {
                            deviceFeatures[i].state = Convert.ToBoolean(Convert.ToInt32(words[index]));
                            if (deviceFeatures[i].state)
                                deviceFeatures[i].value = 255;
                            else
                                deviceFeatures[i].value = 0;
                        }
                        if (switchPortsOnly[i] == 'p')
                        {
                            if (Convert.ToDouble(words[index]) == 0)
                                deviceFeatures[i].state = false;
                            else
                                deviceFeatures[i].state = true;
                            deviceFeatures[i].value = Convert.ToDouble(words[index]);
                        }
                        tl.LogMessage("SH.QueryDeviceStatus", "switch " + i + " value " + deviceFeatures[i].value);
                        index++;
                    }
                    // now iterate through the ports to update the current sensors
                    for (int i = 0; i < switchPortsOnly.Length; i++)
                    {
                        int j = i + switchPortsOnly.Length;
                        deviceFeatures[j].state = true;
                        deviceFeatures[j].value = Convert.ToDouble(words[index]);
                        tl.LogMessage("SH.QueryDeviceStatus", "switch " + j + " value " + deviceFeatures[j].value);
                        index++;
                    }
                    // now do the input ports
                    int p = switchPortsOnly.Length * 2;
                    deviceFeatures[p].state = true;
                    deviceFeatures[p].value = Convert.ToDouble(words[index]);
                    tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                    index++;
                    p++;
                    deviceFeatures[p].state = true;
                    deviceFeatures[p].value = Convert.ToDouble(words[index]);
                    tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                    index++;
                    p++;
                    // now skip the PWM port modes and offsets if they exist
                    if (havePWM)
                    {
                        p += (2 * ( BoardSignature.Split('p').Length - 1));
                        tl.LogMessage("SH.QueryDeviceStatus", "skipped PWM ports");

                    }
                    // and finaly the temp and humid sensors if they are present in the board signature
                    // the board will report 'f' and 't' only if an SHT31 or AHT10 sensor is attached at power-on
                    if (BoardSignature.Contains("f"))
                    {
                        // temperature
                        deviceFeatures[p].state = true;
                        deviceFeatures[p].value = Convert.ToDouble(words[index++]);
                        tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                        // humidity
                        deviceFeatures[++p].state = true;
                        deviceFeatures[p].value = Convert.ToDouble(words[index++]);
                        tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                        // dewpoint
                        deviceFeatures[++p].state = true;
                        deviceFeatures[p].value = Convert.ToDouble(words[index++]);
                        tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                        p++;
                    }
                    if (BoardSignature.Contains("t"))
                    {
                        int i = BoardSignature.IndexOf('t');
                        while (BoardSignature.IndexOf("t", i++) != -1)
                        {
                            deviceFeatures[p].state = true;
                            deviceFeatures[p].value = Convert.ToDouble(words[index++]);
                            tl.LogMessage("SH.QueryDeviceStatus", "switch " + p + " value " + deviceFeatures[p].value);
                            p++;
                        }
                    }
                }
            }
            else
            {
                tl.LogMessage("SH.QueryDeviceStatus", "Invalid response from device: " + response);
                throw new ASCOM.DriverException("Invalid response from device: " + response);
            }
        }

        /// <summary>
        /// queries the device for a geometry ( physical cionfiguration ) and configures the driver
        /// </summary>
        private static void QueryDeviceDescription()
        {
            CheckConnected("QueryDeviceDescription");

            tl.LogMessage("SH.QueryDeviceDescription", "Sending request to device...");
            string response = CommandString(GETDESCRIPTION, false);
            tl.LogMessage("SH.QueryDeviceDescription", "Response from device: " + response);
            // response should be of the form:
            // D:BigPowerBox:001:mmmmmmmmppppaatffff
            string[] words = response.Split(':');
            if (words.Length > 0)
            {
                if (words[0] != "D")
                {
                    tl.LogMessage("SH.QueryDeviceDescription", "Invalid response from device: " + response);
                    throw new ASCOM.DriverException("Invalid response from device: " + response);
                }
                else
                {
                    deviceName = words[1];
                    hwRevision = words[2];
                    BoardSignature = words[3];
                }
            }
            tl.LogMessage("SH.QueryDeviceDescription", "got BoardSignature: " + BoardSignature);

            // translate BoardSignature into an array of Features_t
            // first the electrical ports, the board signature has the types of ports in order followed by
            // the optional temp and humidity sensors. We also need to add the input Amps and Input Volts that
            // do not appear in the signature
            // so in order: port statuses, port currents, input A, input V, Temp, Hunidity
            // first create a new string without temps and humid
            string portsonly = BoardSignature.Replace("t", string.Empty);
            portsonly = portsonly.Replace("f", string.Empty);
            portNum = portsonly.Length;
            tl.LogMessage("SH.QueryDeviceDescription", "got portsOnly: " + portsonly);
            int switchable = 1;
            int pwm = 1;
            int ao = 1;
            int portindex = 0;  // for logging purposes only
            havePWM = false;
            deviceFeatures.Clear();
            for (int i = 0; i < portNum; i++)
            {
                // create all ports as status = false (off), they will be updated later by QueryDeviceStatus()
                Feature_c feature = new Feature_c();
                switch (portsonly[i])
                {
                    case 's':
                        // normal switch port, is RW bool
                        feature.canWrite = true;
                        feature.state = false;
                        feature.type = SWH;
                        feature.port = i + 1;
                        feature.value = 0;
                        feature.minvalue = 0;
                        feature.maxvalue = 1;
                        feature.unit = null;
                        feature.description = "Switchable Port " + switchable++;
                        feature.name = "port " + (i + 1);
                        deviceFeatures.Add(feature);
                        tl.LogMessage("SH.QueryDeviceDescription", "Added SWH port at index: " + portindex++);
                        break;
                    case 'm':
                        // multiplexed switch port, is RW bool
                        feature.canWrite = true;
                        feature.state = false;
                        feature.type = MPX;
                        feature.port = i + 1;
                        feature.value = 0;
                        feature.minvalue = 0;
                        feature.maxvalue = 1;
                        feature.unit = null;
                        feature.description = "Switchable Port " + switchable++;
                        feature.name = "port " + (i + 1);
                        deviceFeatures.Add(feature);
                        tl.LogMessage("SH.QueryDeviceDescription", "Added MPX port at index: " + portindex++);
                        break;
                    case 'p':
                        // pwm switch port, is RW analog
                        feature.canWrite = true;
                        feature.state = false;
                        feature.type = PWM;
                        feature.port = i + 1;
                        feature.value = 0;
                        feature.minvalue = 0;
                        feature.maxvalue = 255;
                        feature.unit = null;
                        feature.description = "PWM Port " + pwm;
                        feature.name = "PWM port " + pwm++;
                        deviceFeatures.Add(feature);
                        tl.LogMessage("SH.QueryDeviceDescription", "Added PWM port at index: " + portindex++);
                        havePWM = true;
                        break;
                    case 'a':
                        // Allways-On port, is RO analog 
                        // do we really need this?
                        feature.canWrite = false;
                        feature.state = false;
                        feature.type = AON;
                        feature.port = i + 1;
                        feature.value = 0;
                        feature.minvalue = 0;
                        feature.maxvalue = 1;
                        feature.unit = null;
                        feature.description = "Always-On Port " + ao++;
                        feature.name = "AO port " + (i + 1);
                        deviceFeatures.Add(feature);
                        tl.LogMessage("SH.QueryDeviceDescription", "Added AON port at index: " + portindex++);
                        break;
                }
            }
            // now again lets loop to create "ports" for the output current sensors
            for (int i = 0; i < portNum; i++)
            {
                Feature_c feature = new Feature_c();
                feature.canWrite = false;
                feature.state = true;
                feature.type = CURRENT;
                feature.port = i + 1;
                feature.value = 0;
                feature.minvalue = 0;
                feature.maxvalue = 50.00;
                feature.unit = "A";
                feature.description = "Output Current Sensor";
                feature.name = "port " + (i + 1) + " Amps";
                deviceFeatures.Add(feature);
                tl.LogMessage("SH.QueryDeviceDescription", "Added CURRENT port at index: " + portindex++);
            }
            // now create "port" for the input current sensor
            {
                Feature_c feature = new Feature_c();
                feature.canWrite = false;
                feature.state = true;
                feature.type = INPUTA;
                feature.port = portNum + 1;
                feature.value = 0;
                feature.minvalue = 0;
                feature.maxvalue = 50.00;
                feature.unit = "A";
                feature.description = "Input Current Sensor";
                feature.name = "Input Amps";
                deviceFeatures.Add(feature);
                tl.LogMessage("SH.QueryDeviceDescription", "Added INPUT CURRENT port at index: " + portindex++);
            }
            // now create "port" for the input voltage sensor
            {
                Feature_c feature = new Feature_c();
                feature.canWrite = false;
                feature.state = true;
                feature.type = INPUTV;
                feature.port = portNum + 2;
                feature.value = 0;
                feature.minvalue = 0;
                feature.maxvalue = 50.00;
                feature.unit = "V";
                feature.description = "Input Voltage Sensor";
                feature.name = "Input Volts";
                deviceFeatures.Add(feature);
                tl.LogMessage("SH.QueryDeviceDescription", "Added INPUT VOLT port at index: " + portindex++);
            }
            // if we have PWM ports lets add the mode and offset selectors
            if (havePWM)
            {
                pwm = 1;
                int firstPWMPortIndex = portsonly.IndexOf("p");
                while (portsonly.IndexOf("p", firstPWMPortIndex) != -1)
                {
                    Feature_c feature2 = new Feature_c();
                    feature2.canWrite = true;
                    feature2.state = false;
                    feature2.type = MODE;
                    feature2.port = firstPWMPortIndex + 1;
                    feature2.value = 0;
                    feature2.minvalue = 0;
                    feature2.maxvalue = 3;
                    feature2.unit = null;
                    feature2.description = "PWM Port " + pwm + " Mode (0: variable, 1: on/off, 2:Dewheater, 3:temperature PID";
                    feature2.name = "PWM Port " + pwm + " Mode";
                    deviceFeatures.Add(feature2);
                    tl.LogMessage("SH.QueryDeviceDescription", "Added MODE port at index: " + portindex++);
                    Feature_c feature3 = new Feature_c();
                    feature3.canWrite = true;
                    feature3.state = false;
                    feature3.type = SETTEMP;
                    feature3.port = firstPWMPortIndex + 1;
                    feature3.value = 0;
                    feature3.minvalue = 0;
                    feature3.maxvalue = 10;
                    feature3.unit = null;
                    feature3.description = "PWM Port " + pwm + " Temp Offset";
                    feature3.name = "PWM Port " + pwm++ + " Offset";
                    deviceFeatures.Add(feature3);
                    tl.LogMessage("SH.QueryDeviceDescription", "Added SETTEMP port at index: " + portindex++);
                    firstPWMPortIndex++;
                }
            }
            // now lets add "ports" for the temp and humidity sensors if they are present
            if (BoardSignature.Contains("f"))
            {
                Feature_c feature = new Feature_c();
                feature.canWrite = false;
                feature.state = true;
                feature.type = TEMP;
                feature.port = portNum + 3;
                feature.value = 0;
                feature.minvalue = -100.00;
                feature.maxvalue = 200.00;
                feature.unit = "C";
                feature.description = "Environment Temperature Sensor";
                feature.name = "Env Temperature";
                deviceFeatures.Add(feature);
                tl.LogMessage("SH.QueryDeviceDescription", "Added ENV TEMP port at index: " + portindex++);
                Feature_c feature2 = new Feature_c();
                feature2.canWrite = false;
                feature2.state = true;
                feature2.type = HUMID;
                feature2.port = portNum + 4;
                feature2.value = 0;
                feature2.minvalue = 0;
                feature2.maxvalue = 100;
                feature2.unit = "%";
                feature2.description = "Environment Humidity Sensor";
                feature2.name = "Env Humidity";
                deviceFeatures.Add(feature2);
                tl.LogMessage("SH.QueryDeviceDescription", "Added ENV HUMID port at index: " + portindex++);
                Feature_c feature3 = new Feature_c();
                feature3.canWrite = false;
                feature3.state = true;
                feature3.type = DEWPOINT;
                feature3.port = portNum + 5;
                feature3.value = 0;
                feature3.minvalue = -100;
                feature3.maxvalue = 200;
                feature3.unit = "C";
                feature3.description = "Environment Dewpoint";
                feature3.name = "Env dewpoint";
                deviceFeatures.Add(feature3);
                tl.LogMessage("SH.QueryDeviceDescription", "Added ENV DEW port at index: " + portindex++);
            }
            if (BoardSignature.Contains("t"))
            {
                int port = 1;
                int i = BoardSignature.IndexOf('t');
                while (BoardSignature.IndexOf("t", i++) != -1)
                {
                    Feature_c feature = new Feature_c();
                    feature.canWrite = false;
                    feature.state = true;
                    feature.type = TEMP;
                    feature.port = port;
                    feature.value = 0;
                    feature.minvalue = -100.00;
                    feature.maxvalue = 200.00;
                    feature.unit = "C";
                    feature.description = "Temperature Sensor for PWM port " + port;
                    feature.name = "Temperature " + port++;
                    deviceFeatures.Add(feature);
                    tl.LogMessage("SH.QueryDeviceDescription", "Added TEMP port at index: " + portindex++);
                }
            }
            // the number of "switches" we want the client to display in the UI ( relates to MaxSwitches )
            numSwitch = (short)deviceFeatures.Count;
            tl.LogMessage("SH.QueryDeviceDescription", "Total number of ports found: " + numSwitch);
        }

        /// <summary>
        /// queries the device for the names of all the ports
        /// the port names are stored in EEPROM
        /// </summary>
        private static void QueryPortNames()
        {
            CheckConnected("QueryPortNames");

            // ">N:%02d#" return ">N:%02d:%s#"
            for (short id = 0; id < numSwitch; id++)
            {
                if (deviceFeatures[id].type <= CURRENT)
                {
                    int newid;
                    string suffix = string.Empty;
                    if (id >= portNum)
                    {
                        newid = id - portNum;
                        suffix = " Current (A)";
                    }
                    else
                        newid = id;
                    string response = CommandString(string.Format(">N:{0, 0:D2}#", newid), false);
                    deviceFeatures[id].name = response.Split(':')[2] + suffix;
                }
                tl.LogMessage("SH.QueryPortNames", $"QueryPortNames({id}) - {deviceFeatures[id].name}");
            }
        }

        /// <summary>
        /// Copies the port names from the setup dialog to the device
        /// </summary>
        private static void CopyNamesToDevice()
        {
            short i = 0;
            foreach (fakePort_c configuredPort in configPortNames)
            {
                if (i + portNum < deviceFeatures.Count)
                {
                    SetSwitchName(i, configuredPort.Name);
                }
                i++;
            }
        }

        /// <summary>
        /// Method that runs in the worker thread to update all port statuses
        /// </summary>
        private static void updateStatus()
        {
            while (true)
            {
                // update every UPDATEINTERVAL millis
                Thread.Sleep(UPDATEINTERVAL);
                // do the update only if we are allowed to
                if (UpdateCanRun)
                {
                    QueryDeviceStatus();
                }
                else
                {
                    // we are not allowed so lets sleep indefinitely 
                    LogMessage("SH.updateStatus", "Thread '{0}' sleeping indefinitely", Thread.CurrentThread.Name);
                    try
                    {
                        Thread.Sleep(Timeout.Infinite);
                    }
                    catch (ThreadInterruptedException)
                    {
                        // time to wake up and resume work
                        LogMessage("SH.updateStatus", "Thread '{0}' awoken.", Thread.CurrentThread.Name);
                    }
                }
            }
        }

        #endregion
    }
}

