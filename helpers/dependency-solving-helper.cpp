#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include <bonsole_client.h>
#include <dbus/dbus.h>


static const char *app_name_real = "PackageKit - dependency solver";
static char *app_name[] = {"title", NULL, NULL};

struct reader_info {
  char *buffer;
  int   curr_old;
  int   loaded;
  int   buff_len;
};

struct window {
  
  xmlNodePtr message;
};

struct application {

  struct window window;
  int output;
  int error_output, messages_output;
};

static void reader_info_init(struct reader_info *str)
{
  str->buffer = NULL;
  str->curr_old = 0;
  str->loaded = 0;
  str->buff_len = 0;
}

static char *get_record(int fd, struct reader_info *info)
{
  int count = 0;
  bool done = false;
  int curr = 0;
  int curr2 = info->curr_old;
  
  
  while (info->loaded >= curr2 + 1) {
    
    
    if ('\0' == info->buffer[curr2]) {
    
      curr = info->curr_old ; 
      info->curr_old = curr2 + 1;
      return &info->buffer[curr];
    }
    
    ++curr2;
  }
  
  info->buff_len += 512;
  info->buffer = (char*)realloc(info->buffer, info->buff_len);
  
  if (NULL == info->buffer) {
    
    return NULL;
  }
  
  while ((count = read(fd, &info->buffer[info->loaded], info->buff_len - 1 - info->loaded)) > 0)  {
    
    
    curr = info->loaded;
    info->loaded += count;
    while ('\0' != info->buffer[curr] && curr < info->loaded) {
      
      ++curr;
    }
    
    if (curr < info->loaded && '\0' == info->buffer[curr]) {
      
      done = true;
      break;
    }
    
    info->buff_len += 512;
    info->buffer = (char*)realloc(info->buffer, info->buff_len);
    
    if (NULL == info->buffer) {
      
      count = 0;
      break;
    }
  };
  
  if (!done && 0 > count) {
    
    perror("Error while read from pipe");
    free(info->buffer);
    close(fd);
    return NULL;
  }
  
  info->buffer[info->loaded] = '\0';
  
  curr2 = info->curr_old;
  info->curr_old = curr + 1;
  return &info->buffer[curr2];
  
}

static bool show_solutions(int fd, struct reader_info *in_ch_reader)
{
  char *buffer, *prev, *curr, *prev2, *prev3;
  int length, length2;
  int problem, solution;
  xmlNodePtr root, text, anchor, message, form, checkbox, group, header, line, content;
  
  xmlDocPtr a = bonsole_window(nullptr);
  root = xmlDocGetRootElement(a);

  //bonsole_set_context_params(0, app_name);
  
  form = xmlNewNode(NULL, BAD_CAST "form");
  xmlSetProp(form, BAD_CAST "action",BAD_CAST "app:update");
  xmlAddChild(root, form);
  
  problem = 0;
  buffer = NULL;
  
  group= xmlNewNode( NULL, BAD_CAST "dl");
  
  xmlAddChild(form, group);
  while ((buffer = get_record(fd, in_ch_reader)) && ('\0' != buffer[0])) {
    
    solution = 0;
    
    text = xmlNewText(BAD_CAST buffer);

    header = xmlNewNode( NULL, BAD_CAST "dt");
    content = xmlNewNode( NULL, BAD_CAST "dd");
    xmlAddChild(header, text);
    xmlAddChild(group, header);
    xmlAddChild(group, content);

    while ((buffer = get_record(fd, in_ch_reader)) && ('\0' != buffer[0])) {
    
      
        checkbox = xmlNewNode(NULL, BAD_CAST "checkbox");
        text = xmlNewNode(NULL, BAD_CAST "br");
        
        xmlAddChild(content, text);
        xmlAddChild(content, checkbox);
        
        if ('\0' != buffer[0]) {
          text = xmlNewText(BAD_CAST buffer);
          xmlAddChild(content, text);
          text = xmlNewNode(NULL, BAD_CAST "br");
          xmlAddChild(content, text);
        }
      
      
      if ((buffer = get_record(fd, in_ch_reader))&& ('\0' != buffer[0])) {
        
        char *startpos = strdup(buffer);
        char *prev = startpos;
        char *curr = prev;
        
        
        while ('\0' != *curr) {
          while ('\0' != *curr
            && '\n' != *curr) ++curr;
          
          if ('\n' == *curr &&
              '\0' == curr[1]) {
          
            *curr = '\0';
             curr += 2;
          }
          else {
            *curr = '\0'; 
            ++curr;
          }
          
          if ('\0' != *curr)  {
            
            text = xmlNewText(BAD_CAST prev);
            xmlAddChild(content, text);
            text = xmlNewNode(NULL, BAD_CAST "br");
            xmlAddChild(content, text);
            prev = curr;
          }
        
        
        }
        
        text = xmlNewText(BAD_CAST prev);
        xmlAddChild(content, text);
        
        if (startpos)
           free(startpos);
        }
        length = snprintf(NULL, 0, "%d_%d", problem, solution) + 1;
        buffer = (char*) malloc(length);
        snprintf(buffer, length, "%d_%d", problem, solution);
        
        xmlSetProp(checkbox, BAD_CAST "name", BAD_CAST buffer);
        
        buffer = NULL;
        
        ++solution;
        
        text = xmlNewNode(NULL, BAD_CAST "br");
        xmlAddChild(content, text);

    }
    ++problem;
  }
  
  
  if (0 == problem) {
    
    root = xmlDocGetRootElement(a);
    
    text = xmlNewText(BAD_CAST "Done. You can close this page.");
    message = xmlNewNode(NULL, BAD_CAST "message");
    xmlAddChild(message, text);
    xmlAddChild(root, message);
    bonsole_window_release(nullptr);
    bonsole_flush_changes(nullptr);
    return false;
  }
  bonsole_window_release(nullptr);
  bonsole_flush_changes(nullptr);
  
  return true;
}

static void message_proc(const char *msg__, intptr_t usr_p)
{
  char *buffer, *prev, *curr, *prev2, *prev3, *spec;
  int length, length2;
  int problem, solution;
  xmlNodePtr root, text, anchor, message, form, checkbox;
  
  struct application *app = (struct application *) usr_p;
  char *msg_ = bonsole_message_unescape_string(msg__, 0);
  
  
  if (0 == strncmp("update?", msg_, sizeof("update?") - 1)) {
    
    //bonsole_reset_document(nullptr);
    xmlDocPtr a = bonsole_window(nullptr);
    
    root = xmlDocGetRootElement(a);
    
    spec = msg_;
    
    buffer = spec;
    
    length = 0;
    
    while ('\0' != *buffer) {
      
      
      if ('&' == *buffer) {
        
        
        ++length;
        
        *buffer = '\0';
        
      }
      ++buffer;
      
    }
    ++length;
    
    buffer = &spec[sizeof("update?") - 1];
    
    
    while (0 < length) {
      
      
      prev = buffer;
      do {
        
        
        ++buffer;
      } while ('_' != *buffer);
      
      *buffer = '\0';
      
      ++buffer;
      
      do {
        
        ++buffer;
      } while ('=' != *buffer);
      
      
      ++buffer;
      
      if ('\0' != buffer[0] && 0 != strcmp(buffer, "1")) {
        
        
        --length;
        do {
          
          ++buffer;
        } while ('\0' != *buffer);
        
        continue;
      }
      
      while ('\0' != *buffer) {
        
        ++buffer;
      }
      
      
      ++buffer;
      
      curr = prev;
      
      prev = buffer;
      
      length2 = length - 1;
      
      while (0 < length2) {
        prev2 = buffer;
        
        
        
        do {
          
          ++buffer;
          
        } while ('_' != *buffer);
        
        
        *buffer = '\0';
        
        ++buffer;
        
        
        if (0 != strcmp(curr, prev2)) {
          
          
          --length2;
          do {
            
            ++buffer;
          } while ('\0' != *buffer);
          
          continue;
        }
        
        
        do {
          ++buffer;
          
        } while ('=' != *buffer);
        
        
        ++buffer;
        
        if ('\0' != buffer[0] && 0 != strcmp(buffer, "1")) {
          
          --length2;
          
          do {
            ++buffer;
            
          } while ('\0' != *buffer);
          
          ++buffer;
          
          if ('\0' != *buffer) {
            
            ++buffer;
            
          }
          continue;
        }
        //message = xmlNewNode(NULL, BAD_CAST "message");
        
        
        xmlChar *ent = xmlEncodeEntitiesReentrant(a, BAD_CAST "You checked two different solutions for one problem");
        
        xmlNodeSetContent(app->window.message, ent);
        
        free(ent);
        
        bonsole_window_release(nullptr);
        
        bonsole_flush_changes(nullptr);
        
       // show_solutions(usr_p);
        
        
        write(app->output, "STOP", sizeof("STOP"));
        
        return;
        
      }
      
      do {
        ++buffer;
        
      } while ('\0' != *buffer);
      int problem_number, solution_number;
      
      
      problem_number = atoi(curr);
      
      while ('\0' != *curr) ++curr;
      
      
      while ('\0' != *curr && '=' != *curr) ++curr;
      
      *curr = '\0';
    
      prev3 = ++curr;
      
      solution_number = atoi(prev3);
      
      free(spec);
      
      int length = (int) (snprintf(NULL, 0, "SELECTION:%d:%d", problem_number, solution_number)) + 2; 

      char *buffer = (char*) malloc(length);
      snprintf(buffer, length, "SELECTION:%c%d:%d", '\0',problem_number, solution_number);
      if (1 > write(app->output, buffer, length)) {
      
        
      }
      
      free(buffer);
      --length;
    }
    
    if (1 > write(app->output, "DONE!", sizeof("DONE!"))) {
      
      
    }
    
    xmlChar *ent = xmlEncodeEntitiesReentrant(a, BAD_CAST "Processing ...");
    
    xmlNodeSetContent(app->window.message, ent);
    
    free(ent);
   
    bonsole_window_release(nullptr);
    bonsole_flush_changes(nullptr);  
    bonsole_quit_loop(nullptr);
    
  }
  
  free(msg_);
}


int main(int argc, char **argv)
{
  struct application app;
  struct reader_info i_ch_reader;
  int curr;
  int input, output, fd;
  int dup_0, dup_1, dup_2;
  
  output = input = -1;
  
  for (curr = 1; curr < argc; ++curr) {
  
    if (0 == strcmp(argv[curr], "--comm-channel-input")) {
    
      if (argc - 1 <= curr) {
      
        exit(1);
      }
      
      input = atoi(argv[curr+1]);
      curr++;
    }
    else if (0 == strcmp(argv[curr], "--comm-channel-output")) {
    
      if (argc - 1 <= curr) {
        
        exit(1);
      }
     
     output = atoi(argv[curr+1]);
     curr++;
    }
    else {
    
      exit(1);
    }
  }
  printf("COMM_CH_OUTPUT: %d COMM_CH_INPUT: %d\n", output, input);

  reader_info_init(&i_ch_reader);
  if (-1 != output) {
    
    int flags = fcntl(output, F_GETFL, 0);
    fcntl(output, F_SETFL, flags | O_NONBLOCK);
  }
  

  
  char *sender = get_record(input, &i_ch_reader);
  
  dup_0 = dup(0);
  dup_1 = dup(1);
  dup_2 = dup(2);
  

  DBusConnection *bus_connection;
  DBusError error;
  
  dbus_error_init(&error);
  bus_connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  if (bus_connection == NULL) {
    if (dbus_error_is_set(&error)) {
      fprintf(stderr,"Error occurred, while trying to connect: %s\n", error.message);
    }
    else {
      fprintf(stderr,"Error occurred, while trying to connect\n");
    }
    dbus_error_free(&error);
    exit(1);
  }
  
  
  char *server, *cookie;
  DBusPendingCall* pending;
  DBusMessage* reply;
  DBusMessageIter args;
  
  
  DBusMessage* msg = dbus_message_new_method_call("pl.art.lach.slawek.apps.DaemonUI","/pl/art/lach/slawek/apps/DaemonUI", "pl.art.lach.slawek.apps.DaemonUI.client", "getListenerNameForClient");
  
  if (msg == NULL) {
    
    goto exit;
  }
  
  
  dbus_message_iter_init_append(msg, &args);
  
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sender)) { 
    
    fprintf(stderr, "Out Of Memory!\n"); 
    exit(1);
  }
  
  if (!dbus_connection_send_with_reply(bus_connection, msg, &pending, -1))
  {
    dbus_message_unref(msg);
    goto exit;
  }
  dbus_connection_flush(bus_connection);
  dbus_message_unref(msg);
  
  dbus_pending_call_block(pending);
  reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);
  
  if (NULL == reply) {
    
    write(output, "ERR:\nNo reply\n", sizeof("ERR\n:No reply\n"));
    puts("Error: No reply");
    goto exit;
  }
  
  if(dbus_message_get_type(reply) ==  DBUS_MESSAGE_TYPE_ERROR)    {
    
    puts("Error");
    char *emsg;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &emsg,  DBUS_TYPE_INVALID)) {
      
      write(output, "ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon", sizeof("ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon"));
      puts("No error message provided");
      goto  exit;
    }
    
    write(output, "ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon", sizeof("ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon"));
    puts(emsg);
    goto exit;
  }
  
  dbus_error_init(&error);
  dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &server, DBUS_TYPE_STRING, &cookie, DBUS_TYPE_INVALID);
  if (dbus_error_is_set(&error)) {
    
    puts(error.message);
    goto exit;
  }
  
  if (NULL == server || NULL == cookie
    || '\0' == server[0] || '\0' == cookie[0]) {
    
    write(output, "ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon", sizeof("ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon"));
    goto exit;
  }
  
  msg = dbus_message_new_method_call("pl.art.lach.slawek.apps.DaemonUI","/pl/art/lach/slawek/apps/DaemonUI", "pl.art.lach.slawek.apps.DaemonUI.client", "getRealTTYForClient");
  
  if (msg == NULL) {
    
    goto exit;
  }
  
  
  dbus_message_iter_init_append(msg, &args);
  
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sender)) { 
    
    fprintf(stderr, "Out Of Memory!\n"); 
    exit(1);  
  }
  
  if (!dbus_connection_send_with_reply(bus_connection, msg, &pending, -1))
  {
    dbus_message_unref(msg);
    goto exit;
  }
  dbus_connection_flush(bus_connection);
  dbus_message_unref(msg);
  
  dbus_pending_call_block(pending);
  reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);
  
  if (NULL == reply) {
    
    puts("Error: No reply");
    write(output, "ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon", sizeof("ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon"));
    goto exit;
  }
  
  if(dbus_message_get_type(reply) ==  DBUS_MESSAGE_TYPE_ERROR)    {
    
    puts("Error");
    char *emsg;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &emsg,  DBUS_TYPE_INVALID)) {
      
      puts("No error message provided");
      goto  exit;
    }
    
    puts(emsg);
    write(output, "ERR:\0Error message obtained\n", sizeof("ERR:\0Error message obtained\n"));
    goto exit;
  }
  
  
  dbus_bool_t error_1;
  
  dup_0 = dup_1 = dup_2 = -1;
  dbus_error_init(&error);
  dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &error_1, DBUS_TYPE_UNIX_FD, &fd, DBUS_TYPE_INVALID);
  
  if (-1 == fd) {
    
    write(output, "ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon", sizeof("ERR:\0No reply: Possible causes daemonUI as system-wide daemon or as session daemon"));
    goto exit;
  }
  
  if (dbus_error_is_set(&error)) {
    
    puts(error.message);
    
    write(output, "ERR:\0DBUSError is set\n", sizeof("ERR:\0DBUSError i set\n"));
    goto exit;
  }
#if 1
  dup_0 = dup(0);
  dup_1 = dup(1);
  dup_2 = dup(2);
  close(0);
  close(1);
  close(2);
  
  
  if (!error_1 && -1 != fd) {
    
    
    
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
  }
#endif
  setenv("HOME", "/root", 0);
  setenv("LANG", "EN_US", 0);
  
  setenv("BONSOLE_DBUS_SCOPE", "SYSTEM_BUS", 1);
  setenv("BONSOLE_RUN_MODE", "ALWAYS_TRY_TO_LOGIN", 1);
  setenv("BONSOLE_DBUS_NAME", server, 1);
  setenv("BONSOLE_COOKIE", cookie, 1);
  {
    
    
  puts(server);
  puts(cookie);  
  int argc = 1;
  char *argv[2] = {(char*)"packagekitd", (char*)NULL};
  
  
  //app_name[1] = bonsole_escape_quotes(app_name_real);
  
  if (0 != bonsole_client_init(&argc, argv)) exit(1);
  }

  app.error_output = dup_2;
  app.messages_output = dup_1;
do {
bonsole_reset_document(nullptr);
if (!show_solutions( input, &i_ch_reader)) {

  break;
}

xmlNodePtr root, text, message;

xmlDocPtr a = bonsole_window(nullptr);
root = xmlDocGetRootElement(a);


message = xmlNewNode(NULL, BAD_CAST "message");

xmlNodeSetContent(message, BAD_CAST " ");

xmlAddChild(root, message);

app.window.message = message;
app.output = output;

bonsole_main_loop(0, message_proc, (intptr_t)(void*) &app);

if (i_ch_reader.buffer) free(i_ch_reader.buffer);

reader_info_init(&i_ch_reader);
} while (true);
exit:
  dup2(dup_0, 0);
  dup2(dup_1, 1);
  dup2(dup_2, 2);
  return EXIT_SUCCESS;
}
