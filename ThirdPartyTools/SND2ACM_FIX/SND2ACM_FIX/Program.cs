using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

using SND2ACM_FIX.Properties;

namespace SND2ACM_FIX
{
    static class Program
    {
        struct WavHeader {
            internal Int16 channels;
            internal Int32 sample_rate;
        };
        
        //[STAThread]
        static void Main(string[] args)
        {
            List<string> files = new List<string>(args);
            
            Console.WriteLine("Wrapper Snd2ACM.exe with fixes acm files.");
            Console.WriteLine("-----------------------------------------");
            
            if (files.Count == 0) {
                Console.WriteLine("Use the command line with a list of files:\nSND2ACM_FIX.exe [-qPercent] file1.wav file2.wav ...\n\nor use the Drag and Drop method to convert the WAV files.");
                Console.ReadKey();
                return;
            }

            string acmExe = Application.StartupPath + "\\snd2acm.exe";
            File.Delete(acmExe);
            File.WriteAllBytes(acmExe, Resources.SND2ACM);
            //File.SetAttributes(path, FileAttributes.Hidden);

            string qPercent = " -q"; 

            string option = files[0]; 
            if (option.StartsWith("-q")) {
                int n = option.IndexOf("-q") + 2;

                qPercent += " " + option.Substring(n).Trim();
                files.RemoveAt(0);
            } else {
                qPercent += "0";
            }

            foreach (var wavFile in files)
            {
                if (!wavFile.EndsWith(".wav", StringComparison.OrdinalIgnoreCase)) continue; 
                int n = wavFile.LastIndexOf('.');
                string acmFile = wavFile.Remove(n) + ".acm";
                
                Console.WriteLine("Convert file: " + wavFile + " to " + acmFile);

                string cmd = @"""" + wavFile + @""" """ + acmFile + @"""" + qPercent;
                ProcessStartInfo psi = new ProcessStartInfo(acmExe, cmd);
                //psi.UseShellExecute = false;
                //psi.CreateNoWindow = true;
                psi.WorkingDirectory = Application.StartupPath;
                Process p = Process.Start(psi);
                p.WaitForExit();
                bool error = (p.ExitCode != 0);
                p.Close();

                if (error) Console.WriteLine(wavFile + " converting file error.");

                if (!error && wavFile != string.Empty && File.Exists(acmFile) && File.Exists(wavFile)) {
                    BinaryReader wav = new BinaryReader(File.OpenRead(wavFile));

                    WavHeader wavData;
                    wav.BaseStream.Seek(22, SeekOrigin.Begin);
                    wavData.channels = wav.ReadInt16();
                    wavData.sample_rate = wav.ReadInt32();
                    wav.Close();

                    if (wavData.sample_rate != 22050 || wavData.channels != 2) {
                        BinaryWriter acm = new BinaryWriter(File.OpenWrite(acmFile));

                        if (wavData.channels != 2) {
                            acm.BaseStream.Seek(8, SeekOrigin.Begin);
                            acm.Write(wavData.channels);
                            Console.WriteLine(acmFile + ": correct channels.");       
                        }
                        if (wavData.sample_rate != 22050) {
                            acm.BaseStream.Seek(10, SeekOrigin.Begin);
                            acm.Write((Int16)wavData.sample_rate);
                            Console.WriteLine(acmFile + ": correct sample rate.");       
                        }
                        acm.Close();
                    }
                }
                Console.WriteLine("");
            }

            Console.WriteLine("Done!");
            System.Threading.Thread.Sleep(1000);
            File.Delete(acmExe);

            Console.ReadKey();
        }

        //static void p_OutputDataReceived(object sender, DataReceivedEventArgs e)
        //{
        //    Console.WriteLine(e.Data);     
        //}
    }
}
