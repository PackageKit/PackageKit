using System;
using PackageKit;

public class TestPK
{
	static void Main ()
	{
		Gtk.Application.Init ();
		Client client = new Client ();
		try {
			client.InstallPackages (new string[] {"sqlite2;0.0.1;i386;fedora"});
			client.Finished += HandleFinished;

		} catch (GLib.GException gex) {
			Console.WriteLine (gex.Message);
		}
		Gtk.Window w = new Gtk.Window ("b");
		w.ShowAll ();
		Gtk.Application.Run ();
	}
	static void HandleFinished (object sender, EventArgs args)
	{
		Console.WriteLine ("Finished!");
	}
}
