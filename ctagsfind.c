#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>

#define die(format, ...)                            \
    do {                                            \
        fprintf(stderr, format"\n", ##__VA_ARGS__); \
        exit(1);                                    \
    } while (0)
#define warn(format, ...)                                  \
    do {                                                   \
        if (verbose)                                       \
            fprintf(stderr, format"\n", ##__VA_ARGS__);    \
    } while (0)
#define need_optarg(opt, errmsg)                        \
    do {                                                \
        if (i + 1 == argc)                              \
            die("Invalid option " opt ": %s", errmsg);  \
    } while (0)
#define optarg argv[++i]

char *tag_file;
char *tag_file_dir;
int meta_tag_allowed = 1;
int tag_file_format = 1;
const char *query;
int query_len;
bool ignore_case;
char *class_filter, *enum_filter, *file_filter;
char *kind_filter, *struct_filter, *union_filter;
struct filter {
    const char *name;
    char **filter;
} filters[] = {
    { .name = "class",  .filter = &class_filter  },
    { .name = "enum",   .filter = &enum_filter   },
    { .name = "file",   .filter = &file_filter   },
    { .name = "kind",   .filter = &kind_filter   },
    { .name = "struct", .filter = &struct_filter },
    { .name = "union",  .filter = &union_filter  },
    { .name = NULL,     .filter = NULL           }
};
bool verbose;

char *xstrdup(const char *s)
{
    char *r = malloc(strlen(s) + 1);
    if (!r)
        die("Bad alloc in xstrdup");
    strcpy(r, s);
    return r;
}

void usage(const char *program)
{
    printf("Usage: %s [options] query\n"
           "  -f tag-file     - Tag file to find\n"
           "  -F name=value   - Add field filter\n"
           "  -i              - Ignore case\n"
           "  -v              - Verbose output\n"
           "  -h              - Show this help\n",
           program);
}

char *next_tag_field(char *);

char *address_following_next_tag_field(char *tag)
{
    char *p = tag;

    if (!p)
        return NULL;
    if (isdigit(*p)) {
        /* Line number format.  */
        while (isdigit(*p)) p++;
    } else if (*p == '/' || *p == '?') {
        char d = *p;
        char c;
        bool escape = false;
        while (c = *++p) {
            if (c == d && !escape) {
                p++;
                break;
            } else if (c == '\\') {
                escape = !escape;
            } else
                escape = false;
        }
    } else {
        warn("Unexpected address format starting with `%c'", *p);
        return NULL;
    }

    char *next = next_tag_field(p);
    if (tag_file_format == 2) {
        /* Skip ;" here.  */
        if (*p != ';' || *(p + 1) != '"')
            warn("Expected `;\"', but not found");
        else
            *p = '\0';
    }
    return next;
}

char *next_tag_field(char *tag)
{
    if (!tag)
        return NULL;
    char *p = strchr(tag, '\t');
    if (!p)
        return NULL;
    *p = '\0';
    return p + 1;
}

void process_meta_tag(char *name, char *value)
{
    if (!strcmp(name, "!_TAG_FILE_FORMAT")) {
        int format = atoi(value);
        if (format != 1 && format != 2)
            warn("Invalid format number: %d", format);
        else
            tag_file_format = format;
    }
}

void process_tag(char *name,  char *next)
{
    char *file = next;
    char *addr = next_tag_field(file);
    char *field = address_following_next_tag_field(addr);
    char *kind;

    while (field) {
        char *key = NULL;
        char *value;
        char *next = next_tag_field(field);
        if (field[0] != '\0' && field[1] == '\0') {
            /* Short notation for kind.  */
            key = "kind";
            kind = value = field;
        } else {
            char *delim = strchr(field, ':');
            if (delim) {
                key = field;
                value = delim + 1;
                *delim = '\0';
            } else {
                warn("Invalid field format: %s", field);
                goto next;
            }
        }

        char **filter = NULL;
        for (struct filter *f = filters; f->name; f++) {
            if (!strcmp(f->name, key)) {
                filter = f->filter;
                break;
            }
        }
        if (filter && *filter && strcmp(value, *filter))
            return;

    next:
        field = next;
    }

    char path[strlen(tag_file_dir) + strlen(file) + 2];
    strcpy(path, tag_file_dir);
    strcat(path, "/");
    strcat(path, file);

    if (addr)
        printf("%s\t%s\n", path, addr);
    else
        printf("%s\n", path);
}

void process_tag_line(char *line, int lineno)
{
    char *name = line;
    char *next = next_tag_field(name);
    if (!next) {
        warn("Invalid tag line at %d", lineno);
        return;
    }

    if (meta_tag_allowed && !strncmp(name, "!_TAG_", 6)) {
        next_tag_field(next);
        process_meta_tag(name, next);
        return;
    } else
        meta_tag_allowed = 0;

    int namelen = next - name - 1;
    bool match = false;
    if (namelen == query_len) {
        if (ignore_case)
            match = !strncasecmp(name, query, query_len);
        else
            match = !strncmp(name, query, query_len);
    }

    if (match)
        process_tag(name, next);
}

void process_tag_file(FILE *in)
{
    char buf[4096];
    char *ptr = buf;
    char *start = buf;
    size_t bufsize = sizeof(buf);
    int lineno = 1;
    bool ignore = false;

    while (1) {
        size_t read = fread(ptr, 1, bufsize - (ptr - buf), in);
        if (read == 0)
            break;

        bool first = true;
        while (1) {
            char *end = memchr(ptr, '\n', read);
            if (!end)
                end = memchr(ptr, '\r', read);
            if (!end) {
                if (first) {
                    if (!ignore)
                        warn("Ignoring too long line at %d", lineno);
                    ptr = start = buf;
                    ignore = true;
                } else {
                    memmove(buf, ptr, read);
                    ptr = buf + read;
                    start = buf;
                }
                break;
            }
            
            if (ignore)
                ignore = false;
            else {
                *end = '\0';
                process_tag_line(start, lineno);
            }

            read -= end - ptr + 1;
            ptr = start = end + 1;
            lineno++;
            first = false;
        }
    }
}

char **parse_options(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-') {
            char opt;
            while (opt = *++arg) {
                switch (opt) {
                case 'f':
                    need_optarg("-f", "No tag file specified");
                    tag_file = xstrdup(optarg);
                    break;
                case 'i':
                    ignore_case = 1;
                    break;
                case 'F': {
                    need_optarg("-F", "No filter specified");
                    char *arg = optarg;
                    char *p = strchr(arg, '=');
                    if (!p)
                        die("Filter must be <name>=<value>");

                    int len = p - arg;
                    for (struct filter *f = filters; f->name; f++) {
                        if (!strncmp(arg, f->name, len)) {
                            *f->filter = p + 1;
                            goto found;
                        }
                    }
                    die("Unknown filter: %s", arg);
                    
                    found:
                    break;
                }
                case 'v':
                    verbose = 1;
                    break;
                case 'h':
                    usage(argv[0]);
                    exit(0);
                    break;
                default:
                    die("Uknown option: -%c", opt);
                    break;
                }
            }
        } else
            return &argv[i];
    }
    return NULL;
}

char *find_tag_file()
{
    char buf[1024];
    char *path = buf;
    if (!getcwd(path, sizeof(buf) - 16))
        die("Too long path");
    while (1) {
        char *last = strchr(path, '\0');
        strcat(last, "/tags");
        if (!access(path, R_OK))
            return xstrdup(path);

        *last = '\0';
        strcpy(path, dirname(path));
        if (strchr(path, '\0') == last)
            break;
    }
    return xstrdup("tags");
}

int main(int argc, char *argv[])
{
    char **rest = parse_options(argc, argv);
    if (!rest)
        die("No query specified");
    else {
        query = *rest;
        query_len = strlen(query);
    }

    if (!tag_file)
        tag_file = find_tag_file();
    tag_file_dir = xstrdup(tag_file);
    strcpy(tag_file_dir, dirname(tag_file_dir));

    FILE *in = fopen(tag_file, "rb");
    if (!in)
        die("Cannot open tag file: %s", tag_file);
    process_tag_file(in);
    fclose(in);

    free(tag_file);
    free(tag_file_dir);

    return 0;
}
