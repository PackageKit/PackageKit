/*
 * UpdateSystem.cs
 *
 * Author(s):
 *	Stephane Delcroix  (stephane@delcroix.org)
 *
 * Copyright (c) 2009 Novell, Inc.
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
using System;
using PackageKit;
using GLib;

public class UpdateSystem
{
	static MainLoop mainloop;
	static void Main ()
	{
		GType.Init ();
		mainloop = new MainLoop ();

		Client client = new Client ();
		client.Finished += HandleFinished;
		client.Message += HandleMessage;
		client.UpdateSystem ();
		mainloop.Run ();
	}

	static void HandleFinished (object sender, EventArgs args)
	{
		Console.WriteLine ("Done updating");
		mainloop.Quit ();

	}

	static void HandleMessage (object sender, MessageArgs args)
	{
		Console.WriteLine ("Got a message:\t{0}", args.Details);
	}
}
