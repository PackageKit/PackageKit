#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

#include <bonsole_client.h>
#include <dbus/dbus.h>
static char *get_record(int fd)
{
  static char *buffer = NULL;
  static int curr_old = 0;
  static int loaded = 0;
  static int buff_len = 0;
  int count = 0;
  bool done = false;
  int curr = 0;
  int curr2 = curr_old;
  
  
  while (loaded >= curr2 + 1) {
    
    
    if ('\0' == buffer[curr2]) {
    
      curr = curr_old ; 
      curr_old = curr2 + 1;
      return &buffer[curr];
    }
    
    ++curr2;
  }
  
  buff_len += 512;
  buffer = (char*)realloc(buffer, buff_len);
  
  if (NULL == buffer) {
    
    return NULL;
  }
  
  while ((count = read(fd, &buffer[loaded], buff_len - 1 - loaded)) > 0)  {
    
    
    curr = loaded;
    loaded += count;
    while ('\0' != buffer[curr] && curr < loaded) {
      
      ++curr;
    }
    
    if (curr < loaded && '\0' == buffer[curr]) {
      
      done = true;
      break;
    }
    
    buff_len += 512;
    buffer = (char*)realloc(buffer, buff_len);
    
    if (NULL == buffer) {
      
      count = 0;
      break;
    }
  };
  
  if (!done && 0 > count) {
    
    perror("Error while read from pipe");
    free(buffer);
    close(fd);
    return NULL;
  }
  
  buffer[loaded] = '\0';
  
  curr2 = curr_old;
  curr_old = curr + 1;
  return &buffer[curr2];
  
}

static bool show_solutions(int fd)
{
  char *buffer, *prev, *curr, *prev2, *prev3;
  int length, length2;
  int problem, solution;
  xmlNodePtr root, text, anchor, message, form, checkbox;
  
  xmlDocPtr a = bonsole_window(nullptr);
  root = xmlDocGetRootElement(a);

  
  form = xmlNewNode(NULL, BAD_CAST "form");
  xmlSetProp(form, BAD_CAST "action",BAD_CAST "app:update");
  xmlAddChild(root, form);
  
  problem = 0;
  solution = 0;
  buffer = NULL;
  
  while ((buffer = get_record(fd)) && ('\0' != buffer[0])) {
    
    
    text = xmlNewText(BAD_CAST buffer);
    
    xmlAddChild(form, text);
    
    
    text = xmlNewNode(NULL, BAD_CAST "br");
    xmlAddChild(form, text);
    
    while ((buffer = get_record(fd)) && ('\0' != buffer[0])) {
    
      checkbox = xmlNewNode(NULL, BAD_CAST "checkbox");
      
      char *prev = buffer;
      char *curr = buffer;
      
      text = xmlNewText(BAD_CAST prev);
      xmlAddChild(checkbox, text);
      
      xmlAddChild(form, checkbox);
      
      if ((buffer = get_record(fd)) && ('\0' != buffer[0])) {
        
        char *prev = buffer;
        char *curr = buffer;
        
        while ('\0' != *curr) {
          
          if ('\n' == *curr) {
            
            *curr = '\0';
            
            text = xmlNewText(BAD_CAST prev);
            xmlAddChild(checkbox, text);
            text = xmlNewNode(NULL, BAD_CAST "br");
            xmlAddChild(checkbox, text);
            prev = curr + 1;
          }
          
          ++curr;
        }
        
        text = xmlNewText(BAD_CAST prev);
        xmlAddChild(checkbox, text);
        
      }
        length = snprintf(NULL, 0, "%d_%d", problem, solution) + 1;
        buffer = (char*) malloc(length);
        snprintf(buffer, length, "%d_%d", problem, solution);
        
        xmlSetProp(checkbox, BAD_CAST "name", BAD_CAST buffer);
        
        buffer = NULL;
      
      ++solution;

      text = xmlNewNode(NULL, BAD_CAST "br");
      xmlAddChild(form, text);
    }
    text = xmlNewNode(NULL, BAD_CAST "br");
    xmlAddChild(form, text);
    ++problem;
  }
  
  
  if (0 == problem)
    return false;
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
  
  char *msg_ = bonsole_message_unescape_string(msg__, 0);
  
  if (0 == strncmp("update?", msg_, sizeof("update?") - 1)) {
    
    bonsole_reset_document(nullptr);
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
        message = xmlNewNode(NULL, BAD_CAST "message");
        text = xmlNewText(BAD_CAST "You checked two different solutions for one problem");
        xmlAddChild(message, text);   
        xmlAddChild(root, message);   
        
        bonsole_window_release(nullptr);
        show_solutions(usr_p);
        
        return;
        
      }
      do {
        ++buffer;
      } while ('\0' != *buffer);
      int problem_number, solution_number;
      
      problem_number = atoi(curr);
      while ('\0' != *curr) ++curr;
      prev3 = ++curr;
      while ('\0' != *curr && '=' != *curr) ++curr;
      *curr = '\0';
      solution_number = atoi(prev3);
      //++solution_number;
      
      
      
      free(spec);
      
      
      --length;
    }
    
    
    
    bonsole_window_release(nullptr);
    bonsole_flush_changes(nullptr);
    
    bonsole_quit_loop(nullptr);
  }
  
  free(msg_);
}


int main(int argc, char **argv)
{
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
  
  char *sender = get_record(input);
  
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
    
    puts("Error: No reply");
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
    goto exit;
  }
  
  dbus_error_init(&error);
  dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &server, DBUS_TYPE_STRING, &cookie, DBUS_TYPE_INVALID);
  if (dbus_error_is_set(&error)) {
    
    puts(error.message);
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
    goto exit;
  }
  dbus_bool_t error_1;
  
  dup_0 = dup_1 = dup_2 = -1;
  dbus_error_init(&error);
  dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &error_1, DBUS_TYPE_UNIX_FD, &fd, DBUS_TYPE_INVALID);
  if (dbus_error_is_set(&error)) {
    
    puts(error.message);
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
  int argc = 1;
  char *argv[2] = {(char*)"packagekitd", (char*)NULL};
  
  if (0 != bonsole_client_init(&argc, argv)) exit(1);
  }

#if 0
transaction_problems.problems = problems;
transaction_problems.it = problems.begin();
transaction_problems.resolver = zypp->resolver ();
transaction_problems.solution_list = NULL;
#endif
do {
bonsole_reset_document(nullptr);
if (!show_solutions( input)) {

  break;
}
bonsole_main_loop(0, message_proc, output);
} while (true);
#if 0
add_resolution_to_zypp(&transaction_problems);

// Save resolution to file
save_transaction_to_cache("Install", path_to_cache, &transaction_problems, 
                          priv->to_install, priv->to_remove);
#endif
exit:
  dup2(dup_0, 0);
  dup2(dup_1, 1);
  dup2(dup_2, 2);
  return EXIT_SUCCESS;
}
