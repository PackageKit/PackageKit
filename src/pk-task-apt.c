/**
 * pk_task_parse_data:
 **/
static void
pk_task_parse_data (PkTask *task, const gchar *line)
{
	char **sections;
	gboolean okay;

	/* check if output line */
	if (strstr (line, " - ") == NULL)
		return;		
	sections = g_strsplit (line, " - ", 0);
	okay = pk_task_filter_package_name (NULL, sections[0]);
	if (okay == TRUE) {
		pk_debug ("package='%s' shortdesc='%s'", sections[0], sections[1]);
		pk_task_package (task, sections[0], sections[1]);
	}
	g_strfreev (sections);
}

