from packagekit.enums import GROUP_ACCESSIBILITY, GROUP_ACCESSORIES, GROUP_EDUCATION, GROUP_GAMES, GROUP_GRAPHICS, GROUP_INTERNET, GROUP_MULTIMEDIA, GROUP_OFFICE, GROUP_OTHER,  GROUP_PROGRAMMING,  GROUP_SYSTEM

"""
Classify the Categories and help me to make a backend

#    from XMLCache import XMLCache
#    print "categoryMap = {",
#    for i in XMLCache()._getAllCategories():
#        print "'%s':" % i
#    print "}",

"""

categoryMap = {
'VectorGraphics': GROUP_GRAPHICS,
'Network': GROUP_INTERNET,
'Spreadsheet': GROUP_OFFICE,
'Application': GROUP_OTHER,
'X-GNOME-NetworkSettings': GROUP_INTERNET,
'Music': GROUP_MULTIMEDIA,
'P2P': GROUP_INTERNET,
'WordProcessor': GROUP_OFFICE,
'X-GNOME-PersonalSettings': GROUP_SYSTEM,
'Presentation': GROUP_OFFICE,
'Email': GROUP_OFFICE,
'Monitor': GROUP_SYSTEM,
'Development': GROUP_PROGRAMMING,
'Core': GROUP_SYSTEM,
'RasterGraphics': GROUP_GRAPHICS,
'Telephony': GROUP_INTERNET,
'Photography': GROUP_GRAPHICS,
'HardwareSettings': GROUP_SYSTEM,
'News': GROUP_INTERNET,
'X-SuSE-Core-Office': GROUP_SYSTEM,
'X-Red-Hat-Base': GROUP_SYSTEM,
#'GNOME': GROUP_OTHER,
'Settings': GROUP_SYSTEM,
#'GTK': GROUP_OTHER,
'System': GROUP_SYSTEM,
'Graphics': GROUP_GRAPHICS,
'X-Ximian-Main': GROUP_OFFICE,
'Security': GROUP_SYSTEM,
'Audio': GROUP_MULTIMEDIA,
'ContactManagement': GROUP_ACCESSORIES,
'X-Novell-Main': GROUP_OFFICE,
'AudioVideo': GROUP_MULTIMEDIA,
'WebDevelopment': GROUP_PROGRAMMING,
'X-GNOME-SystemSettings': GROUP_SYSTEM,
'Office': GROUP_OFFICE,
'Viewer': GROUP_ACCESSORIES,
'Player': GROUP_OTHER,
'DesktopSettings': GROUP_SYSTEM,
'WebBrowser': GROUP_INTERNET,
'Utility': GROUP_ACCESSORIES,
'GUIDesigner': GROUP_PROGRAMMING,
'TerminalEmulator': GROUP_ACCESSORIES,
'Video': GROUP_MULTIMEDIA ,
'Education': GROUP_EDUCATION,
'Game': GROUP_GAMES ,
'Building': GROUP_PROGRAMMING,
'Debugger': GROUP_PROGRAMMING,
'IDE': GROUP_PROGRAMMING,
'GUIDesigner': GROUP_PROGRAMMING,
'Profiling': GROUP_PROGRAMMING,
'RevisionControl': GROUP_PROGRAMMING, #	Applications like cvs or subversion	Development
'Database': GROUP_PROGRAMMING, #	Application to manage a database	Office or Development or AudioVideo
'Translation': GROUP_PROGRAMMING,#	A translation tool	Development
'Calendar': GROUP_OFFICE,#	Calendar application	Office
'ContactManagement':GROUP_OFFICE,#	E.g. an address book	Office
'Dictionary': GROUP_OFFICE,#	A dictionary	Office;TextTools
'Chart': GROUP_OFFICE,#	Chart application	Office
'Finance':GROUP_OFFICE,#	Application to manage your finance	Office
'FlowChart': GROUP_OFFICE,#	A flowchart application	Office
'PDA': GROUP_OFFICE, #	Tool to manage your PDA	Office
'ProjectManagement': GROUP_OFFICE,#	Project management application	Office;Development
'Presentation': GROUP_OFFICE,#	Presentation software	Office
'Spreadsheet': GROUP_OFFICE,#	A spreadsheet	Office
'WordProcessor':GROUP_OFFICE, #	A word processor	Office
'2DGraphics': GROUP_GRAPHICS,#	2D based graphical application	Graphics
'VectorGraphics': GROUP_GRAPHICS,#	Vector based graphical application	Graphics;2DGraphics
'RasterGraphics': GROUP_GRAPHICS, #	Raster based graphical application	Graphics;2DGraphics
'3DGraphics': GROUP_GRAPHICS, #	3D based graphical application	Graphics
'Scanning': GROUP_GRAPHICS,#	Tool to scan a file/text	Graphics
'OCR': GROUP_GRAPHICS, #	Optical character recognition application	Graphics;Scanning
'Photography': GROUP_GRAPHICS, #Camera tools, etc.	Graphics or Office
'Publishing' : GROUP_GRAPHICS, #	Desktop Publishing applications and Color Management tools	Graphics or Office
'Viewer': GROUP_OFFICE, #Tool to view e.g. a graphic or pdf file	Graphics or Office
'TextTools': GROUP_ACCESSORIES, #A text tool utiliy	Utility
'DesktopSettings': GROUP_SYSTEM,#	Configuration tool for the GUI	Settings
'HardwareSettings': GROUP_SYSTEM, #	A tool to manage hardware components, like sound cards, video cards or printers	Settings
'Printing': GROUP_SYSTEM, #	A tool to manage printers	HardwareSettings;Settings
'PackageManager': GROUP_SYSTEM, #	A package manager application	Settings
'Dialup': GROUP_INTERNET,#	A dial-up program	Network
'InstantMessaging': GROUP_INTERNET, #	An instant messaging client	Network
'Chat': GROUP_INTERNET,#	A chat client	Network
'IRCClient': GROUP_INTERNET,#	An IRC client	Network
'FileTransfer': GROUP_INTERNET, #	Tools like FTP or P2P programs	Network
'HamRadio': GROUP_INTERNET, #	HAM radio software	Network or Audio
'News': GROUP_INTERNET, #	A news reader or a news ticker	Network
'P2P': GROUP_INTERNET, #	A P2P program	Network
'RemoteAccess': GROUP_INTERNET, #	A tool to remotely manage your PC	Network
'Telephony': GROUP_INTERNET,#	Telephony via PC	Network
'TelephonyTools': GROUP_INTERNET, #	Telephony tools, to dial a number, manage PBX, ...	Utility
'VideoConference': GROUP_INTERNET, #	Video Conference software	Network
'WebBrowser': GROUP_INTERNET, #	A web browser	Network
'WebDevelopment': GROUP_INTERNET, #	A tool for web developers	Network or Development
'Midi': GROUP_MULTIMEDIA, #	An app related to MIDI	AudioVideo;Audio
'Mixer': GROUP_MULTIMEDIA,#	Just a mixer	AudioVideo;Audio
'Sequencer':GROUP_MULTIMEDIA,#	A sequencer	AuioVideo;Audio
'Tuner': GROUP_MULTIMEDIA,#	A tuner	AudioVideo;Audio
'TV': GROUP_MULTIMEDIA,#	A TV application	AudioVideo;Video
'AudioVideoEditing': GROUP_MULTIMEDIA,#	Application to edit audio/video files	Audio or Video or AudioVideo
'Player': GROUP_MULTIMEDIA,#	Application to play audio/video files	Audio or Video or AudioVideo
'Recorder': GROUP_MULTIMEDIA,#	Application to record audio/video files	Audio or Video or AudioVideo
'DiscBurning':GROUP_MULTIMEDIA,#	Application to burn a disc	AudioVideo
'ActionGame':GROUP_GAMES,#	An action game	Game
'AdventureGame':GROUP_GAMES,#	Adventure style game	Game
'ArcadeGame':GROUP_GAMES,#	Arcade style game	Game
'BoardGame':GROUP_GAMES,#	A board game	Game
'BlocksGame':GROUP_GAMES,#	Falling blocks game	Game
'CardGame':GROUP_GAMES,#	A card game	Game
'KidsGame': GROUP_GAMES,#	A game for kids	Game
'LogicGame':GROUP_GAMES,#	Logic games like puzzles, etc	Game
'RolePlaying':GROUP_GAMES,#	A role playing game	Game
'Simulation':GROUP_GAMES,#	A simulation game	Game
'SportsGame': GROUP_GAMES, #A sports game	Game
'StrategyGame': GROUP_GAMES,#	A strategy game	Game Art	Software to teach arts	Education
'Construction':GROUP_GAMES,#	 	Education
'Music': GROUP_MULTIMEDIA,#	Musical software	AudioVideo;Education
'Languages':GROUP_EDUCATION,#	Software to learn foreign languages	Education
'Science': GROUP_EDUCATION,#	Scientific software	Education
'ArtificialIntelligence': GROUP_EDUCATION, #	Artificial Intelligence software	Education;Science
'Astronomy': GROUP_EDUCATION,#	Astronomy software	Education;Science
'Biology':GROUP_SYSTEM,#	Biology software	Education;Science
'Chemistry':GROUP_EDUCATION,#	Chemistry software	Education;Science
'ComputerScience':GROUP_EDUCATION,#	ComputerSience software	Education;Science
'DataVisualization':GROUP_EDUCATION,#	Data visualization software	Education;Science
'Economy':GROUP_EDUCATION,#	Economy software	Education
'Electricity':GROUP_EDUCATION,#	Electricity software	Education;Science
'Geography':GROUP_EDUCATION,#	Geography software	Education
'Geology':GROUP_EDUCATION,#	Geology software	Education;Science
'Geoscience':GROUP_EDUCATION,#	Geoscience software	Education;Science
'History':GROUP_EDUCATION,#	History software	Education
'ImageProcessing':GROUP_EDUCATION,#	Image Processing software	Education;Science
'Literature':GROUP_EDUCATION,#	Literature software	Education
'Math':GROUP_EDUCATION,#	Math software	Education;Science
'NumericalAnalysis':GROUP_EDUCATION,#	Numerical analysis software	Education;Science;Math
'MedicalSoftware':GROUP_EDUCATION,#	Medical software	Education;Science
'Physics':GROUP_EDUCATION,#	Physics software	Education;Science
'Robotics':GROUP_EDUCATION,#	Robotics software	Education;Science
'Sports':GROUP_GRAPHICS,#	Sports software	Education
'ParallelComputing':GROUP_EDUCATION,#	Parallel computing software	Education;Science;ComputerScience
#'Amusement'	A simple amusement
'Archiving': GROUP_ACCESSORIES,#	A tool to archive/backup data	Utility
'Compression': GROUP_ACCESSORIES,#	A tool to manage compressed data/archives	Utility;Archiving
#'Electronics'	Electronics software, e.g. a circuit designer
'Emulator':GROUP_GAMES,#	Emulator of another platform, such as a DOS emulator	System or Game
'Engineering':GROUP_EDUCATION,#	Engineering software, e.g. CAD programs
'FileTools':GROUP_ACCESSORIES, #	A file tool utility	Utility or System
'FileManager':GROUP_ACCESSORIES,#	A file manager	System;FileTools
'TerminalEmulator':GROUP_ACCESSORIES,#	A terminal emulator application	System
'Filesystem':GROUP_SYSTEM, #	A file system tool	System
'Monitor':GROUP_SYSTEM,#	Monitor application/applet that monitors some resource or activity	System
'Security':GROUP_SYSTEM,#	A security tool	Settings or System
'Accessibility':GROUP_ACCESSIBILITY,#	Accessibility	Settings or Utility
'Calculator':GROUP_ACCESSORIES,#	A calculator	Utility
'Clock':GROUP_ACCESSORIES,#	A clock application/applet	Utility
'TextEditor':GROUP_ACCESSORIES,#	A text editor	Utility
'Documentation':GROUP_EDUCATION,#	Help or documentation
'Core':GROUP_SYSTEM,#	Important application, core to the desktop such as a file manager or a help browser
#KDE	Application based on KDE libraries	QT
#GNOME	Application based on GNOME libraries	GTK
#GTK	Application based on GTK+ libraries
#Qt	Application based on Qt libraries
#Motif	Application based on Motif libraries
#Java	Application based on Java GUI libraries, such as AWT or Swing
'ConsoleOnly':GROUP_ACCESSORIES,
}

groupMap = {}

for (con_cat, pk_group) in categoryMap.items():
    if groupMap.has_key(pk_group):
        groupMap[pk_group].append(con_cat)
    else:
        groupMap[pk_group] = [con_cat]

if __name__ == "__main__":
    print groupMap
