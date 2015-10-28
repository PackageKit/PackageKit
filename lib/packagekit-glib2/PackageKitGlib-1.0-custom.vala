
	[CCode (cheader_filename = "packagekit-glib2/packagekit.h")]
	[SimpleType]
	public struct Pk.Bitfield : uint64 {
		public void add (int @enum);
		public bool contain (int @enum);
		public int contain_priority (int value, ...);
		public static Pk.Bitfield from_enums (int value, ...);
		public void invert (int @enum);
		public void remove (int @enum);
		public static void test (void* user_data);
		public static Pk.Bitfield value (int @enum);
	}
