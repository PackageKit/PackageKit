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
}

groupMap = {}

for (con_cat, pk_group) in categoryMap.items():
    if groupMap.has_key(pk_group):
        groupMap[pk_group].append(con_cat)
    else:
        groupMap[pk_group] = [con_cat]

if __name__ == "__main__":
    print groupMap
