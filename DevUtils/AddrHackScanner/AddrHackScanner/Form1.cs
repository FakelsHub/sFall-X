using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace AddrHackScanner
{
    public partial class Form1 : Form
    {
        string searchPath;

        public Form1()
        {
            InitializeComponent();
        }

        private static void Find(string[] text, int start, int end, List<string> result)
        {
            for (int i = 0; i < text.Length; i++)
            {
                int s = 0;
                do {
                    int addr = SubFind(text[i], ref s);
                    if (addr == 0) break;
                    if (addr >= start && addr <= end) {
                        // входит в диапазон
                        result.Add("  Line: " + (i + 1) + " // " + text[i]);
                    }
                } while (s < text[i].Length);
            }
        }

        private static int SubFind(string text, ref int s)
        {
            int addr = 0;
            string addrHex = string.Empty;
            int n = text.IndexOf("0x", s, StringComparison.OrdinalIgnoreCase);
            if (n != -1) {
                s = n + 2;
                for (int m = s; m < text.Length; m++) {
                    if (char.IsLetterOrDigit(text[m])) {
                        addrHex += text[m];
                    } else {
                        s = m;
                        break;
                    }
                }
                Int32.TryParse(addrHex, System.Globalization.NumberStyles.HexNumber, null, out addr);
            }
            return addr;
        }

        private void btnSearch_Click(object sender, EventArgs e)
        {
            int start, end;
            if (!Int32.TryParse(tbStart.Text, System.Globalization.NumberStyles.HexNumber, null, out start)) {
                return;
            }
            if (!Int32.TryParse(tbEnd.Text, System.Globalization.NumberStyles.HexNumber, null, out end)) {
                return;
            }
            if (start > end) return;

            List<string> files = Directory.GetFiles(searchPath, "*.cpp", SearchOption.AllDirectories).ToList();
            files.AddRange(Directory.GetFiles(searchPath, "*.h", SearchOption.AllDirectories).ToList());

            files.Sort();

            List<string> result = new List<string>();

            foreach (var file in files)
            {
                result.Add("File: " + file);
                int count = result.Count;

                Find(File.ReadAllLines(file), start, end, result);

                if (count == result.Count) {
                    result.RemoveAt(count - 1);
                } else {
                    result.Add(string.Empty);
                }
            }

            foreach (var text in result)
            {
                tbResult.Text += text + Environment.NewLine;
            }

        }

        private void btnFolder_Click(object sender, EventArgs e)
        {
            FolderBrowserDialog folder = new FolderBrowserDialog();
            folder.RootFolder = Environment.SpecialFolder.MyComputer;
            folder.SelectedPath = Path.GetDirectoryName(Application.ExecutablePath);
            folder.ShowNewFolderButton = false;

            if (folder.ShowDialog() == System.Windows.Forms.DialogResult.OK) {
                searchPath = folder.SelectedPath;
                btnSearch.Enabled = true;
                this.Text = searchPath;
            }
        }

        private void btnClear_Click(object sender, EventArgs e)
        {
            tbResult.Clear();
        }
    }
}
