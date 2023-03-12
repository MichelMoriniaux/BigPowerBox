//
// ASCOM Switch driver for ShortCircuitBigPowerSwitch
//
// Description:	 ASCOM driver for the Big Power Switch Exxxtreme
//
// Implements:	ASCOM Switch interface version: 0.1
// Author:		(2023) Michel Moriniaux <first.last@gmail.com>
//

using ASCOM;
using ASCOM.Astrometry;
using ASCOM.Astrometry.AstroUtils;
using ASCOM.Astrometry.NOVAS;
using ASCOM.DeviceInterface;
using ASCOM.LocalServer;
using ASCOM.Utilities;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace ASCOM.ShortCircuitBigPowerSwitch.Switch
{
    //
    // This code is mostly a presentation layer for the functionality in the SwitchHardware class. You should not need to change the contents of this file very much, if at all.
    // Most customisation will be in the SwitchHardware class, which is shared by all instances of the driver, and which must handle all aspects of communicating with your device.
    //
    // Your driver's DeviceID is ASCOM.ShortCircuitBigPowerSwitch.Switch
    //
    // The COM Guid attribute sets the CLSID for ASCOM.ShortCircuitBigPowerSwitch.Switch
    // The COM ClassInterface/None attribute prevents an empty interface called _ShortCircuitBigPowerSwitch from being created and used as the [default] interface
    //

    /// <summary>
    /// ASCOM Switch Driver for ShortCircuitBigPowerSwitch.
    /// </summary>
    [ComVisible(true)]
    [Guid("7e9afd02-a203-4e48-a51f-b9273422785c")]
    [ProgId("ASCOM.ShortCircuitBigPowerSwitch.Switch")]
    [ServedClassName("ASCOM Local Server Switch Driver for ShortCircuitBigPowerSwitch")] // Driver description that appears in the Chooser, customise as required
    [ClassInterface(ClassInterfaceType.None)]
    public class Switch : ReferenceCountedObjectBase, ISwitchV2, IDisposable
    {
        internal static string DriverProgId; // ASCOM DeviceID (COM ProgID) for this driver, the value is retrieved from the ServedClassName attribute in the class initialiser.
        internal static string DriverDescription; // The value is retrieved from the ServedClassName attribute in the class initialiser.

        // connectedState holds the connection state from this driver instance's perspective, as opposed to the local server's perspective, which may be different because of other client connections.
        internal bool connectedState; // The connected state from this driver's perspective)
        internal TraceLogger tl; // Trace logger object to hold diagnostic information just for this instance of the driver, as opposed to the local server's log, which includes activity from all driver instances.
        private bool disposedValue;

        #region Initialisation and Dispose

        /// <summary>
        /// Initializes a new instance of the <see cref="ShortCircuitBigPowerSwitch"/> class. Must be public to successfully register for COM.
        /// </summary>
        public Switch()
        {
            try
            {
                // Pull the ProgID from the ProgID class attribute.
                Attribute attr = Attribute.GetCustomAttribute(this.GetType(), typeof(ProgIdAttribute));
                DriverProgId = ((ProgIdAttribute)attr).Value ?? "PROGID NOT SET!";  // Get the driver ProgIDfrom the ProgID attribute.

                // Pull the display name from the ServedClassName class attribute.
                attr = Attribute.GetCustomAttribute(this.GetType(), typeof(ServedClassNameAttribute));
                DriverDescription = ((ServedClassNameAttribute)attr).DisplayName ?? "DISPLAY NAME NOT SET!";  // Get the driver description that displays in the ASCOM Chooser from the ServedClassName attribute.

                // LOGGING CONFIGURATION
                // By default all driver logging will appear in Hardware log file
                // If you would like each instance of the driver to have its own log file as well, uncomment the lines below

                tl = new TraceLogger("", "ShortCircuitBigPowerSwitch.Driver"); // Remove the leading ASCOM. from the ProgId because this will be added back by TraceLogger.
                SetTraceState();

                // Initialise the hardware if required
                SwitchHardware.InitialiseHardware();

                LogMessage("SD.Switch", "Starting driver initialisation");
                LogMessage("SD.Switch", $"ProgID: {DriverProgId}, Description: {DriverDescription}");

                connectedState = false; // Initialise connected to false


                LogMessage("SD.Switch", "Completed initialisation");
            }
            catch (Exception ex)
            {
                LogMessage("SD.Switch", $"Initialisation exception: {ex}");
                MessageBox.Show($"{ex.Message}", "Exception creating ASCOM.ShortCircuitBigPowerSwitch.Switch", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        /// <summary>
        /// Class destructor called automatically by the .NET runtime when the object is finalised in order to release resources that are NOT managed by the .NET runtime.
        /// </summary>
        /// <remarks>See the Dispose(bool disposing) remarks for further information.</remarks>
        ~Switch()
        {
            // Please do not change this code.
            // The Dispose(false) method is called here just to release unmanaged resources. Managed resources will be dealt with automatically by the .NET runtime.

            Dispose(false);
        }

        /// <summary>
        /// Deterministically dispose of any managed and unmanaged resources used in this instance of the driver.
        /// </summary>
        /// <remarks>
        /// Do not dispose of items in this method, put clean-up code in the 'Dispose(bool disposing)' method instead.
        /// </remarks>
        public void Dispose()
        {
            // Please do not change the code in this method.

            // Release resources now.
            Dispose(disposing: true);

            // Do not add GC.SuppressFinalize(this); here because it breaks the ReferenceCountedObjectBase COM connection counting mechanic
        }

        /// <summary>
        /// Dispose of large or scarce resources created or used within this driver file
        /// </summary>
        /// <remarks>
        /// The purpose of this method is to enable you to release finite system resources back to the operating system as soon as possible, so that other applications work as effectively as possible.
        ///
        /// NOTES
        /// 1) Do not call the SwitchHardware.Dispose() method from this method. Any resources used in the static SwitchHardware class itself, 
        ///    which is shared between all instances of the driver, should be released in the SwitchHardware.Dispose() method as usual. 
        ///    The SwitchHardware.Dispose() method will be called automatically by the local server just before it shuts down.
        /// 2) You do not need to release every .NET resource you use in your driver because the .NET runtime is very effective at reclaiming these resources. 
        /// 3) Strong candidates for release here are:
        ///     a) Objects that have a large memory footprint (> 1Mb) such as images
        ///     b) Objects that consume finite OS resources such as file handles, synchronisation object handles, memory allocations requested directly from the operating system (NativeMemory methods) etc.
        /// 4) Please ensure that you do not return exceptions from this method
        /// 5) Be aware that Dispose() can be called more than once:
        ///     a) By the client application
        ///     b) Automatically, by the .NET runtime during finalisation
        /// 6) Because of 5) above, you should make sure that your code is tolerant of multiple calls.    
        /// </remarks>
        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                if (disposing)
                {
                    try
                    {
                        // Dispose of managed objects here

                        // Clean up the trace logger object
                        if (!(tl is null))
                        {
                            tl.Enabled = false;
                            tl.Dispose();
                            tl = null;
                        }
                    }
                    catch (Exception)
                    {
                        // Any exception is not re-thrown because Microsoft's best practice says not to return exceptions from the Dispose method. 
                    }
                }

                try
                {
                    // Dispose of unmanaged objects, if any, here (OS handles etc.)
                }
                catch (Exception)
                {
                    // Any exception is not re-thrown because Microsoft's best practice says not to return exceptions from the Dispose method. 
                }

                // Flag that Dispose() has already run and disposed of all resources
                disposedValue = true;
            }
        }

        #endregion

        // PUBLIC COM INTERFACE ISwitchV2 IMPLEMENTATION

        #region Common properties and methods.

        /// <summary>
        /// Displays the Setup Dialogue form.
        /// If the user clicks the OK button to dismiss the form, then
        /// the new settings are saved, otherwise the old values are reloaded.
        /// THIS IS THE ONLY PLACE WHERE SHOWING USER INTERFACE IS ALLOWED!
        /// </summary>
        public void SetupDialog()
        {
            try
            {
                LogMessage("SD.SetupDialog", $"Calling SetupDialog.");
                SwitchHardware.SetupDialog();
                LogMessage("SD.SetupDialog", $"Completed.");
            }
            catch (Exception ex)
            {
                LogMessage("SD.SetupDialog", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>Returns the list of custom action names supported by this driver.</summary>
        /// <value>An ArrayList of strings (SafeArray collection) containing the names of supported actions.</value>
        public ArrayList SupportedActions
        {
            get
            {
                try
                {
                    CheckConnected($"SupportedActions");
                    ArrayList actions = SwitchHardware.SupportedActions;
                    LogMessage("SD.SupportedActions", $"Returning {actions.Count} actions.");
                    return actions;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.SupportedActions", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>Invokes the specified device-specific custom action.</summary>
        /// <param name="ActionName">A well known name agreed by interested parties that represents the action to be carried out.</param>
        /// <param name="ActionParameters">List of required parameters or an <see cref="String.Empty">Empty String</see> if none are required.</param>
        /// <returns>A string response. The meaning of returned strings is set by the driver author.
        /// <para>Suppose filter wheels start to appear with automatic wheel changers; new actions could be <c>QueryWheels</c> and <c>SelectWheel</c>. The former returning a formatted list
        /// of wheel names and the second taking a wheel name and making the change, returning appropriate values to indicate success or failure.</para>
        /// </returns>
        public string Action(string actionName, string actionParameters)
        {
            try
            {
                CheckConnected($"Action {actionName} - {actionParameters}");
                LogMessage("SD.", $"Calling Action: {actionName} with parameters: {actionParameters}");
                string actionResponse = SwitchHardware.Action(actionName, actionParameters);
                LogMessage("SD.Action", $"Completed.");
                return actionResponse;
            }
            catch (Exception ex)
            {
                LogMessage("SD.Action", $"Threw an exception: \r\n{ex}");
                throw;
            }
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
        public void CommandBlind(string command, bool raw)
        {
            try
            {
                CheckConnected($"CommandBlind: {command}, Raw: {raw}");
                LogMessage("SD.CommandBlind", $"Calling method - Command: {command}, Raw: {raw}");
                SwitchHardware.CommandBlind(command, raw);
                LogMessage("SD.CommandBlind", $"Completed.");
            }
            catch (Exception ex)
            {
                LogMessage("SD.CommandBlind", $"Command: {command}, Raw: {raw} threw an exception: \r\n{ex}");
                throw;
            }
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
        public bool CommandBool(string command, bool raw)
        {
            try
            {
                CheckConnected($"CommandBool: {command}, Raw: {raw}");
                LogMessage("SD.CommandBlind", $"Calling method - Command: {command}, Raw: {raw}");
                bool commandBoolResponse = SwitchHardware.CommandBool(command, raw);
                LogMessage("SD.CommandBlind", $"Returning: {commandBoolResponse}.");
                return commandBoolResponse;
            }
            catch (Exception ex)
            {
                LogMessage("SD.CommandBool", $"Command: {command}, Raw: {raw} threw an exception: \r\n{ex}");
                throw;
            }
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
        public string CommandString(string command, bool raw)
        {
            try
            {
                CheckConnected($"CommandString: {command}, Raw: {raw}");
                LogMessage("SD.CommandString", $"Calling method - Command: {command}, Raw: {raw}");
                string commandStringResponse = SwitchHardware.CommandString(command, raw);
                LogMessage("SD.CommandString", $"Returning: {commandStringResponse}.");
                return commandStringResponse;
            }
            catch (Exception ex)
            {
                LogMessage("SD.CommandString", $"Command: {command}, Raw: {raw} threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Set True to connect to the device hardware. Set False to disconnect from the device hardware.
        /// You can also read the property to check whether it is connected. This reports the current hardware state.
        /// </summary>
        /// <value><c>true</c> if connected to the hardware; otherwise, <c>false</c>.</value>
        public bool Connected
        {
            get
            {
                try
                {
                    // Returns the driver's connection state rather than the local server's connected state, which could be different because there may be other client connections still active.
                    LogMessage("SD.Connected Get", connectedState.ToString());
                    return connectedState;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.Connected Get", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
            set
            {
                try
                {
                    if (value == connectedState)
                    {
                        LogMessage("SD.Connected Set", "Device already connected, ignoring Connected Set = true");
                        return;
                    }

                    if (value)
                    {
                        connectedState = true;
                        LogMessage("SD.Connected Set", "Connecting to device");
                        SwitchHardware.Connected = true;
                    }
                    else
                    {
                        connectedState = false;
                        LogMessage("SD.Connected Set", "Disconnecting from device");
                        SwitchHardware.Connected = false;
                    }
                }
                catch (Exception ex)
                {
                    LogMessage("SD.Connected Set", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// Returns a description of the device, such as manufacturer and model number. Any ASCII characters may be used.
        /// </summary>
        /// <value>The description.</value>
        public string Description
        {
            get
            {
                try
                {
                    CheckConnected($"Description");
                    string description = SwitchHardware.Description;
                    LogMessage("SD.Description", description);
                    return description;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.Description", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// Descriptive and version information about this ASCOM driver.
        /// </summary>
        public string DriverInfo
        {
            get
            {
                try
                {
                    // This should work regardless of whether or not the driver is Connected, hence no CheckConnected method.
                    string driverInfo = SwitchHardware.DriverInfo;
                    LogMessage("SD.DriverInfo", driverInfo);
                    return driverInfo;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.DriverInfo", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// A string containing only the major and minor version of the driver formatted as 'm.n'.
        /// </summary>
        public string DriverVersion
        {
            get
            {
                try
                {
                    // This should work regardless of whether or not the driver is Connected, hence no CheckConnected method.
                    string driverVersion = SwitchHardware.DriverVersion;
                    LogMessage("SD.DriverVersion", driverVersion);
                    return driverVersion;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.DriverVersion", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// The interface version number that this device supports.
        /// </summary>
        public short InterfaceVersion
        {
            get
            {
                try
                {
                    // This should work regardless of whether or not the driver is Connected, hence no CheckConnected method.
                    short interfaceVersion = SwitchHardware.InterfaceVersion;
                    LogMessage("SD.InterfaceVersion", interfaceVersion.ToString());
                    return interfaceVersion;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.InterfaceVersion", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// The short name of the driver, for display purposes
        /// </summary>
        public string Name
        {
            get
            {
                try
                {
                    // This should work regardless of whether or not the driver is Connected, hence no CheckConnected method.
                    string name = SwitchHardware.Name;
                    LogMessage("SD.Name Get", name);
                    return name;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.Name", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        #endregion

        #region ISwitchV2 Implementation

        /// <summary>
        /// The number of switches managed by this driver
        /// </summary>
        /// <returns>The number of devices managed by this driver.</returns>
        public short MaxSwitch
        {
            get
            {
                try
                {
                    CheckConnected("MaxSwitch");
                    short maxSwitch = SwitchHardware.MaxSwitch;
                    LogMessage("SD.MaxSwitch", maxSwitch.ToString());
                    return maxSwitch;
                }
                catch (Exception ex)
                {
                    LogMessage("SD.MaxSwitch", $"Threw an exception: \r\n{ex}");
                    throw;
                }
            }
        }

        /// <summary>
        /// Return the name of switch device n.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The name of the device</returns>
        public string GetSwitchName(short id)
        {
            try
            {
                CheckConnected("GetSwitchName");
                LogMessage("SD.GetSwitchName", $"Calling method.");
                string switchName = SwitchHardware.GetSwitchName(id);
                LogMessage("SD.GetSwitchName", switchName.ToString());
                return switchName;
            }
            catch (Exception ex)
            {
                LogMessage("SD.GetSwitchName", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Set a switch device name to a specified value.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="name">The name of the device</param>
        public void SetSwitchName(short id, string name)
        {
            try
            {
                CheckConnected("SetSwitchName");
                LogMessage("SD.SetSwitchName", $"Calling method.");
                SwitchHardware.SetSwitchName(id, name);
                LogMessage("SD.SetSwitchName", $"Completed.");
            }
            catch (Exception ex)
            {
                LogMessage("SD.SetSwitchName", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Gets the description of the specified switch device. This is to allow a fuller description of
        /// the device to be returned, for example for a tool tip.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>
        /// String giving the device description.
        /// </returns>
        public string GetSwitchDescription(short id)
        {
            try
            {
                CheckConnected("GetSwitchDescription");
                LogMessage("SD.GetSwitchDescription", $"Calling method.");
                string switchDescription = SwitchHardware.GetSwitchDescription(id);
                LogMessage("SD.GetSwitchDescription", switchDescription.ToString());
                return switchDescription;
            }
            catch (Exception ex)
            {
                LogMessage("SD.GetSwitchDescription", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Reports if the specified switch device can be written to, default true.
        /// This is false if the device cannot be written to, for example a limit switch or a sensor.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>
        /// <c>true</c> if the device can be written to, otherwise <c>false</c>.
        /// </returns>
        public bool CanWrite(short id)
        {
            try
            {
                CheckConnected("CanWrite");
                LogMessage("SD.CanWrite", $"Calling method.");
                bool canWrite = SwitchHardware.CanWrite(id);
                LogMessage("SD.CanWrite", canWrite.ToString());
                return canWrite;
            }
            catch (Exception ex)
            {
                LogMessage("SD.CanWrite", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        #region Boolean switch members

        /// <summary>
        /// Return the state of switch device id as a boolean
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>True or false</returns>
        public bool GetSwitch(short id)
        {
            try
            {
                CheckConnected("GetSwitch");
                LogMessage("SD.GetSwitch", $"Calling method.");
                bool getSwitch = SwitchHardware.GetSwitch(id);
                LogMessage("SD.GetSwitch", getSwitch.ToString());
                return getSwitch;
            }
            catch (Exception ex)
            {
                LogMessage("SD.GetSwitch", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Sets a switch controller device to the specified state, true or false.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="state">The required control state</param>
        public void SetSwitch(short id, bool state)
        {
            try
            {
                CheckConnected("SetSwitch");
                LogMessage("SD.SetSwitch", $"Calling method.");
                SwitchHardware.SetSwitch(id, state);
                LogMessage("SD.SetSwitch", $"Completed.");
            }
            catch (Exception ex)
            {
                LogMessage("SD.SetSwitch", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        #endregion

        #region Analogue members

        /// <summary>
        /// Returns the maximum value for this switch device, this must be greater than <see cref="MinSwitchValue"/>.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The maximum value to which this device can be set or which a read only sensor will return.</returns>
        public double MaxSwitchValue(short id)
        {
            try
            {
                CheckConnected("MaxSwitchValue");
                LogMessage("SD.MaxSwitchValue", $"Calling method.");
                double maxSwitchValue = SwitchHardware.MaxSwitchValue(id);
                LogMessage("SD.MaxSwitchValue", maxSwitchValue.ToString());
                return maxSwitchValue;
            }
            catch (Exception ex)
            {
                LogMessage("SD.MaxSwitchValue", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Returns the minimum value for this switch device, this must be less than <see cref="MaxSwitchValue"/>
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The minimum value to which this device can be set or which a read only sensor will return.</returns>
        public double MinSwitchValue(short id)
        {
            try
            {
                CheckConnected("MinSwitchValue");
                LogMessage("SD.MinSwitchValue", $"Calling method.");
                double maxSwitchValue = SwitchHardware.MinSwitchValue(id);
                LogMessage("SD.MinSwitchValue", maxSwitchValue.ToString());
                return maxSwitchValue;
            }
            catch (Exception ex)
            {
                LogMessage("SD.MinSwitchValue", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Returns the step size that this device supports (the difference between successive values of the device).
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The step size for this device.</returns>
        public double SwitchStep(short id)
        {
            try
            {
                CheckConnected("SwitchStep");
                LogMessage("SD.SwitchStep", $"Calling method.");
                double switchStep = SwitchHardware.SwitchStep(id);
                LogMessage("SD.SwitchStep", switchStep.ToString());
                return switchStep;
            }
            catch (Exception ex)
            {
                LogMessage("SD.SwitchStep", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Returns the value for switch device id as a double
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <returns>The value for this switch, this is expected to be between <see cref="MinSwitchValue"/> and
        /// <see cref="MaxSwitchValue"/>.</returns>
        public double GetSwitchValue(short id)
        {
            try
            {
                CheckConnected("GetSwitchValue");
                LogMessage("SD.GetSwitchValue", $"Calling method.");
                double switchValue = SwitchHardware.GetSwitchValue(id);
                LogMessage("SD.GetSwitchValue", switchValue.ToString());
                return switchValue;
            }
            catch (Exception ex)
            {
                LogMessage("SD.GetSwitchValue", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        /// <summary>
        /// Set the value for this device as a double.
        /// </summary>
        /// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
        /// <param name="value">The value to be set, between <see cref="MinSwitchValue"/> and <see cref="MaxSwitchValue"/></param>
        public void SetSwitchValue(short id, double value)
        {
            try
            {
                CheckConnected("SetSwitchValue");
                LogMessage("SD.SetSwitchValue", $"Calling method.");
                SwitchHardware.SetSwitchValue(id, value);
                LogMessage("SD.SetSwitchValue", $"Completed.");
            }
            catch (Exception ex)
            {
                LogMessage("SD.SetSwitchValue", $"Threw an exception: \r\n{ex}");
                throw;
            }
        }

        #endregion

        #endregion

        #region Private properties and methods
        // Useful properties and methods that can be used as required to help with driver development

        /// <summary>
        /// Use this function to throw an exception if we aren't connected to the hardware
        /// </summary>
        /// <param name="message"></param>
        private void CheckConnected(string message)
        {
            if (!connectedState)
            {
                throw new NotConnectedException($"{DriverDescription} ({DriverProgId}) is not connected: {message}");
            }
        }

        /// <summary>
        /// Log helper function that writes to the driver or local server loggers as required
        /// </summary>
        /// <param name="identifier">Identifier such as method name</param>
        /// <param name="message">Message to be logged.</param>
        private void LogMessage(string identifier, string message)
        {
            // This code is currently set to write messages to an individual driver log AND to the shared hardware log.

            // Write to the individual log for this specific instance (if enabled by the driver having a TraceLogger instance)
            if (tl != null)
            {
                tl.LogMessageCrLf(identifier, message); // Write to the individual driver log
            }

            // Write to the common hardware log shared by all running instances of the driver.
            SwitchHardware.LogMessage(identifier, message); // Write to the local server logger
        }

        /// <summary>
        /// Read the trace state from the driver's Profile and enable / disable the trace log accordingly.
        /// </summary>
        private void SetTraceState()
        {
            using (Profile driverProfile = new Profile())
            {
                driverProfile.DeviceType = "Switch";
                tl.Enabled = Convert.ToBoolean(driverProfile.GetValue(DriverProgId, SwitchHardware.traceStateProfileName, string.Empty, SwitchHardware.traceStateDefault));
            }
        }

        #endregion
    }
}
