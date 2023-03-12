using ASCOM.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using static System.Windows.Forms.VisualStyles.VisualStyleElement.Button;

namespace ASCOM.ShortCircuitBigPowerSwitch.Switch
{
    [ComVisible(false)] // Form not registered for COM!
    public partial class SetupDialogForm : Form
    {
        const string NO_PORTS_MESSAGE = "No COM ports found";
        TraceLogger tl; // Holder for a reference to the driver's trace logger

        public SetupDialogForm(TraceLogger tlDriver)
        {
            InitializeComponent();

            this.Load += SetupDialogForm_Load;
            
            // Save the provided trace logger for use within the setup dialogue
            tl = tlDriver;
            // get a copy of the switches for editing
            dataGridViewSwitches.RowsAdded += new DataGridViewRowsAddedEventHandler(dataGridViewSwitches_RowsAdded);

            // Initialise current values of user settings from the ASCOM Profile
            InitUI();
        }

        private void SetupDialogForm_Load(object sender, EventArgs e)
        {
            // Bring the setup dialogue to the front of the screen
            if (WindowState == FormWindowState.Minimized)
                WindowState = FormWindowState.Normal;
            else
            {
                TopMost = true;
                Focus();
                BringToFront();
                TopMost = false;
            }
        }

        private void InitRow(DataGridViewCellCollection cells)
        {
            // init the switch as a boolean read/write switch
            InitCell(cells["colId"], "0");
            InitCell(cells["switchName"], "1");
        }

        private void InitCell(DataGridViewCell cell, object obj)
        {
            if (cell.Value == null)
            {
                cell.Value = obj;
            }
        }

        private void dataGridViewSwitches_RowsAdded(object sender, DataGridViewRowsAddedEventArgs e)
        {
            UpdateRowId();
        }

        private void dataGridViewSwitches_RowsRemoved(object sender, DataGridViewRowsRemovedEventArgs e)
        {
            UpdateRowId();
        }

        /// <summary>
        /// update the "id" column with the switch number
        /// </summary>
        private void UpdateRowId()
        {
            var n = dataGridViewSwitches.Rows.Count;
            for (int i = 0; i < n; i++)
            {
                if (i < this.dataGridViewSwitches.RowCount)
                {
                    if (dataGridViewSwitches.Rows[i].IsNewRow)
                    {
                        continue;
                    }
                    this.dataGridViewSwitches.Rows[i].Cells["colId"].Value = (i+1).ToString();
                    InitRow(this.dataGridViewSwitches.Rows[i].Cells);
                }
            }
        }

        private void CmdOK_Click(object sender, EventArgs e) // OK button event handler
        {
            // Place any validation constraint checks here and update the state variables with results from the dialogue

            tl.Enabled = chkTrace.Checked;

            try
            {
                if (!SwitchHardware.Connected)
                    SwitchHardware.comPort = (string)comboBoxComPort.SelectedItem;
                if (checkBox1.Checked)
                {
                    SwitchHardware.updateNames = true;
                    int count = SwitchHardware.configPortNames.Count;
                    SwitchHardware.configPortNames.Clear();
                    int i = 0;
                    foreach (DataGridViewRow row in dataGridViewSwitches.Rows)
                    {
                        if (i < count)
                            SwitchHardware.configPortNames.Add(new SwitchHardware.fakePort_c(i, (string)row.Cells["switchName"].Value));
                        i++;
                    }
                }
            }
            catch
            {
                // Ignore any errors here in case the PC does not have any COM ports that can be selected
            }
            tl.Enabled = chkTrace.Checked;
        }

        private void CmdCancel_Click(object sender, EventArgs e) // Cancel button event handler
        {
            Close();
        }

        private void BrowseToAscom(object sender, EventArgs e) // Click on ASCOM logo event handler
        {
            try
            {
                System.Diagnostics.Process.Start("https://ascom-standards.org/");
            }
            catch (Win32Exception noBrowser)
            {
                if (noBrowser.ErrorCode == -2147467259)
                    MessageBox.Show(noBrowser.Message);
            }
            catch (Exception other)
            {
                MessageBox.Show(other.Message);
            }
        }

        private void checkBox1_CheckedChanged(object sender, EventArgs e)
        {
            // dataGridViewSwitches.AllowUserToAddRows = checkBox1.Checked;
            // dataGridViewSwitches.AllowUserToDeleteRows = checkBox1.Checked;
            if (!checkBox1.Checked)
            {
                dataGridViewSwitches.ReadOnly = true;
                dataGridViewSwitches.BackgroundColor = Color.LightGray;
                dataGridViewSwitches.ForeColor = Color.Gray;
                dataGridViewSwitches.ColumnHeadersDefaultCellStyle.ForeColor = SystemColors.ControlDark;
                dataGridViewSwitches.EnableHeadersVisualStyles = false;
                dataGridViewSwitches.Invalidate();
                dataGridViewSwitches.Update();
            }
            else
            {
                dataGridViewSwitches.ReadOnly = false;
                dataGridViewSwitches.BackgroundColor = Color.White;
                dataGridViewSwitches.ForeColor = Color.Black;
                dataGridViewSwitches.ColumnHeadersDefaultCellStyle.ForeColor = Color.Black;
                dataGridViewSwitches.EnableHeadersVisualStyles = true;
                dataGridViewSwitches.Invalidate();
                dataGridViewSwitches.Update();
            }
        }

        private void InitUI()
        {
            // setup the port rows
            dataGridViewSwitches.Rows.Clear();
            var i = 0;
            foreach (var item in SwitchHardware.configPortNames)
            {
                dataGridViewSwitches.Rows.Add(i, item.Name);
                i++;
            }
            checkBox1_CheckedChanged(null, null);

            // Set the trace checkbox
            chkTrace.Checked = tl.Enabled;

            // set the Comm port selector
            comboBoxComPort.Items.Clear(); // Clear any existing entries

            // if connected then disable comm port choice
            if (SwitchHardware.Connected)
            {
                comboBoxComPort.Items.Add(SwitchHardware.comPort);
                comboBoxComPort.ForeColor = Color.Gray;
                comboBoxComPort.Enabled = false;
            }
            else
            {
                // set the list of COM ports to those that are currently available
                using (Serial serial = new Serial()) // User the Se5rial component to get an extended list of COM ports
                {
                    comboBoxComPort.Items.AddRange(serial.AvailableCOMPorts);
                }

                // If no ports are found include a message to this effect
                if (comboBoxComPort.Items.Count == 0)
                {
                    comboBoxComPort.Items.Add(NO_PORTS_MESSAGE);
                    comboBoxComPort.SelectedItem = NO_PORTS_MESSAGE;
                }

                // select the current port if possible
                if (comboBoxComPort.Items.Contains(SwitchHardware.comPort))
                {
                    comboBoxComPort.SelectedItem = SwitchHardware.comPort;
                }
            }

            tl.LogMessage("InitUI", $"Set UI controls to Trace: {chkTrace.Checked}, COM Port: {comboBoxComPort.SelectedItem}");
        }
    }
}