/**
 * hn2mbox -- convert HackerNews stories and comments from JSON to mbox format
 *
 * See https://github.com/sytelus/HackerNewsData
 */

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <limits>
#include <list>
#include <string>
#include <unordered_map>

#include "rapidjson/reader.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"

#define printd(...) fprintf(stderr, __VA_ARGS__)
//#define printd(...)

using namespace std;
using namespace rapidjson;

// command line option flags
enum {
    FLAG_SPLIT_MBOX    =  1 << 1,
    FLAG_DUMP_IDS      =  1 << 2,
};

int flags;
// filename containing item ids for the --id-file option
char *idfile = NULL;
// --since and --until options
time_t since = 0;
time_t until = numeric_limits<time_t>::max();

// Either a story or a comment
struct Item {
    string created_at;
    string title;
    string url;
    string author;
    int points;
    string story_text;
    string comment_text;
    unsigned num_comments;
    unsigned story_id;
    string story_title;
    string story_url;
    unsigned parent_id;
    unsigned created_at_i;
    unsigned objectID;
};

// the Item field being currently parsed
enum class Element
{
    none,
    created_at,
    title,
    url,
    author,
    points,
    story_text,
    comment_text,
    num_comments,
    story_id,
    story_title,
    story_url,
    parent_id,
    created_at_i,
    objectID,
};

typedef unordered_map <int, FILE*> Files;
Files outputFiles;
// get the file to output story or comment data based on the passed date
FILE *getFile(struct tm *date)
{
    if (!(flags & FLAG_SPLIT_MBOX))
        return stdout;

    int key = (date->tm_mon & 0xffff) | ((date->tm_year & 0xffff) << 16);
//    printd("key 0x%x\n", key);

    auto iter = outputFiles.find(key);
    if (iter == outputFiles.end()) {
        char fname[100];
        if (!strftime(fname, sizeof fname, "HN-%Y-%m", date)) {
            fprintf(stderr, "wrong date format!?\n");
            exit(1);
        }
        printd("new file: %s\n", fname);
        FILE *file = fopen(fname, "a");
        if (!file) {
            fprintf(stderr, "could not open `%s' for writing: %s\n",
                    fname, strerror(errno));
            exit(1);
        }
        auto res = outputFiles.insert(make_pair(key, file));
        iter = res.first;
        // TODO close past files, for example files two months older than
        // this one
    }

    return iter->second;
}

// https://stackoverflow.com/questions/5665231/most-efficient-way-to-escape-xml-html-in-c-string
string htmlEncode(const string& data)
{
    string buffer;
    buffer.reserve(data.size() * 1.1);
    for(size_t pos = 0; pos != data.size(); ++pos) {
        switch(data[pos]) {
        case '&':  buffer.append("&amp;");       break;
        case '\"': buffer.append("&quot;");      break;
        case '\'': buffer.append("&apos;");      break;
        case '<':  buffer.append("&lt;");        break;
        case '>':  buffer.append("&gt;");        break;
        default:   buffer.append(&data[pos], 1); break;
        }
    }
    return buffer;
}

// output item in mbox format
void dumpItemAsEmail(const Item &item,
                     const unordered_map<unsigned, unsigned> &item_ids)
{
    char datestr[100];
    time_t dt = item.created_at_i;
    if (dt < since || dt >= until) {
//        printd("date out of range %zu %zu %zu\n", since, dt, until);
        return;
    }

    struct tm *date = gmtime(&dt);
    FILE *out = getFile(date);
    if (!strftime(datestr, sizeof datestr, "%a, %d %b %Y %T %z", date))
        *datestr = '\0';

    fprintf(out, "From \n"
            "Message-ID: <%u@hndump>\n"
            "From: %s <%s@hndump>\n"
            "Subject: %s\n"
            "Date: %s\n"
            "Mime-Version: 1.0\n"
            "Content-Type: text/html; charset=utf-8\n",
            item.objectID,
            item.author.c_str(), item.author.c_str(),
            // FIXME: some items have neither title nor story_title
            item.title.empty() ? item.story_title.c_str() : item.title.c_str(),
            datestr);

    if (item.parent_id) { // this item is a comment
        fprintf(out, "In-Reply-To: <%u@hndump>\n", item.parent_id);
        // https://wiki.mozilla.org/MailNews:Message_Threading
        list<unsigned> parents;
        parents.push_front(item.parent_id);
        while (true) {
            auto i = item_ids.find(parents.front());
            if (i == item_ids.end() || i->second == 0)
                break;
            parents.push_front(i->second);
        }
        fprintf(out, "References:");
        for (auto p : parents)
            fprintf(out, " <%u@hndump>", p);
        fprintf(out, "\n");
    }

    fprintf(out, "X-HackerNews-Link: https://news.ycombinator.com/item?id=%u\n",
            item.objectID);
    fprintf(out, "X-HackerNews-Points: %d\n", item.points);
    if (!item.url.empty())
        fprintf(out, "X-HackerNews-Url: %s\n", item.url.c_str());
    if (item.story_id)
        fprintf(out, "X-HackerNews-Story-Link: "
                "https://news.ycombinator.com/item?id=%u\n", item.story_id);
    if (item.parent_id == 0) // this item is a story
        fprintf(out, "X-HackerNews-Num-Comments: %u\n", item.num_comments);

    // FIXME: We're cheating here because, according to RFC 5332, lines
    // should not be longer than 998 chars. But if we fix that by splitting
    // long lines, then we should escape lines starting with "From "
    // (perhaps formatting output as quoted-printable)
    if (item.parent_id) // this item is a comment
        fprintf(out, "\n"
                "<html>%s</html>\n"
                "\n",
                item.comment_text.c_str());
    else
        fprintf(out, "\n"
                "<html><a href=\"%s\" rel=\"nofollow\">%s</a><p>%s</html>\n"
                "\n",
                item.url.c_str(), htmlEncode(item.url).c_str(),
                item.story_text.c_str());
}

template<typename Encoding = UTF8<>>
struct ItemsHandler {
    typedef typename Encoding::Ch Ch;

    ItemsHandler() : element(Element::none), parent(noparent), level(0), item {} {}

    void Default() {}
    void Null() { element = Element::none; }
    void Bool(bool) { Default(); }
    void Int(int i) { Int64(i); }
    void Uint(unsigned i) { Int64(i); }
    void Int64(int64_t i)
    {
        if (parent == _highlightResult)
            return;
        switch (element) {
        case Element::points:         item.points = i;        break;
        case Element::num_comments:   item.num_comments = i;  break;
        case Element::story_id:       item.story_id = i;      break;
        case Element::parent_id:      item.parent_id = i;     break;
        case Element::created_at_i:   item.created_at_i = i;  break;
        default:                      break;
        }

        element = Element::none;
    }
    void Uint64(uint64_t) { Default(); }
    void Double(double) { Default(); }

    void String(const Ch* str, SizeType length, bool copy)
    {
        if (parent == _highlightResult)
            return;
        if (element == Element::none) {
            if (strcmp("hits", str) == 0)
                parent = hits;
            else if (strcmp("_highlightResult", str) == 0)
                parent = _highlightResult;
            else if (strcmp("created_at", str) == 0)
                element = Element::created_at;
            else if (strcmp("title", str) == 0)
                element = Element::title;
            else if (strcmp("url", str) == 0)
                element = Element::url;
            else if (strcmp("author", str) == 0)
                element = Element::author;
            else if (strcmp("points", str) == 0)
                element = Element::points;
            else if (strcmp("story_text", str) == 0)
                element = Element::story_text;
            else if (strcmp("comment_text", str) == 0)
                element = Element::comment_text;
            else if (strcmp("num_comments", str) == 0)
                element = Element::num_comments;
            else if (strcmp("story_id", str) == 0)
                element = Element::story_id;
            else if (strcmp("story_title", str) == 0)
                element = Element::story_title;
            else if (strcmp("story_url", str) == 0)
                element = Element::story_url;
            else if (strcmp("parent_id", str) == 0)
                element = Element::parent_id;
            else if (strcmp("created_at_i", str) == 0)
                element = Element::created_at_i;
            else if (strcmp("objectID", str) == 0)
                element = Element::objectID;
            return;
        }

        if (element == Element::none)
            return;

        // minimal string normalization
        string s = str;
        replace(s.begin(), s.end(), '\n', ' ');
        remove(s.begin(), s.end(), '\r');

        switch (element) {
        case Element::created_at:     item.created_at = s;    break;
        case Element::title:          item.title = s;         break;
        case Element::url:            item.url = s;           break;
        case Element::author:         item.author = s;        break;
        case Element::story_text:     item.story_text = s;    break;
        case Element::comment_text:   item.comment_text = s;  break;
        case Element::story_title:    item.story_title = s;   break;
        case Element::story_url:      item.story_url = s;     break;
        case Element::objectID:       item.objectID = atoi(s.c_str()); break;
        default:                      break;
        }

        element = Element::none;
    }
    void StartObject() { ++level; }
    void EndObject(SizeType)
    {
        if (--level == 1) {
            parent = hits;
            dumpItemAsEmail(item, item_ids);
            item = {};
        }
    }
    void StartArray() { Default(); }
    void EndArray(SizeType) { Default(); }

    Element element;
    enum {
        noparent,
        hits,
        _highlightResult,
    } parent;
    int level;

    Item item;
    // key: objectID, value: parent_id, used to locate an item in its story
    // thread
    unordered_map<unsigned, unsigned> item_ids;
};

unordered_map<unsigned, unsigned>
readIdFile(const char *fname)
{
    unordered_map<unsigned, unsigned> file_ids;
    FILE *file = fopen(fname, "r");
    if (!file) {
        perror("could not open id file");
        exit(1);
    }

    int n;
    unsigned objectID, parent_id;
    while ((n = fscanf(file, "%u\t%u", &objectID, &parent_id)) != EOF) {
        if (n != 2) {
            fprintf(stderr, "bad format in id file %s\n", fname);
            exit(1);
        }
        file_ids.insert(make_pair(objectID, parent_id));
    }

    return file_ids;
}

template<typename Encoding = UTF8<>>
struct ItemIdsHandler {
    typedef typename Encoding::Ch Ch;

    ItemIdsHandler() : element(Element::none), level(0), item {} {}

    void Default() {}
    void Null() { element = Element::none; }
    void Bool(bool) { Default(); }
    void Int(int i) { Int64(i); }
    void Uint(unsigned i) { Int64(i); }
    void Int64(int64_t i)
    {
        if (level > 2)
            return;
        switch (element) {
        case Element::parent_id:   item.parent_id = i;     break;
        default:                   break;
        }
        element = Element::none;
    }
    void Uint64(uint64_t) { Default(); }
    void Double(double) { Default(); }

    void String(const Ch* str, SizeType length, bool copy)
    {
        if (level > 2)
            return;
        if (element == Element::none) {
            if (strcmp("parent_id", str) == 0)
                element = Element::parent_id;
            else if (strcmp("objectID", str) == 0)
                element = Element::objectID;
            return;
        }
        switch (element) {
        case Element::objectID:   item.objectID = atoi(str); break;
        default:                  break;
        }
        element = Element::none;
    }
    void StartObject() { ++level; }
    void EndObject(SizeType)
    {
        if (--level == 1) {
            printf("%u\t%u\n", item.objectID, item.parent_id);
            item = {};
        }
    }
    void StartArray() { Default(); }
    void EndArray(SizeType) { Default(); }

    Element element;
    int level;
    Item item;
};

time_t parsedate(char *datestr, int *err)
{
    struct tm date = {};
    struct tm *dateck = NULL;
    time_t dateepoch = 0;
    int error = 0;
    char *endp = strptime(datestr, "%Y-%m-%d", &date);

    if (endp == NULL || *endp != '\0') {
        error = EINVAL;
        goto out;
    }

    dateepoch = timegm(&date);
    dateck = gmtime(&dateepoch);

    if (!dateck || dateck->tm_year != date.tm_year
            || dateck->tm_mon != date.tm_mon || dateck->tm_mday != date.tm_mday) {
        error = EINVAL;
        goto out;
    }

out:
    if (err)
        *err = error;

    return dateepoch;
}

int main(int argc, char* argv[])
{
    static struct option long_options[] = {
        { "dump-ids",     0,                  NULL,  'd' },
        { "id-file",      required_argument,  NULL,  'i' },
        { "split",        0,                  NULL,  'S' },
        { "since",        required_argument,  NULL,  's' },
        { "until",        required_argument,  NULL,  'u' },
        { NULL,           0,                  NULL,  0 }
    };

    int opt;
    int err;
    while ((opt = getopt_long(argc, argv, "di:Ss:u:",
                              long_options, NULL)) != EOF) {
        switch (opt) {
        case 'd':
            flags |= FLAG_DUMP_IDS;
            break;
        case 'i':
            idfile = optarg; // FIXME strdup()??
            break;
        case 'S':
            flags |= FLAG_SPLIT_MBOX;
            break;
        case 's': {
            since = parsedate(optarg, &err);
            if (err) {
                fprintf(stderr, "invalid format in --since string\n");
                exit(1);
            }
            break;
        }
        case 'u': {
            until = parsedate(optarg, &err);
            if (err) {
                fprintf(stderr, "invalid format in --until string\n");
                exit(1);
            }
            break;
        }

        default:
            fprintf(stderr, "usage:\n"
                    "\thn2mbox --dump-ids\n"
                    "\thn2mbox [--id-file=FILE] [--split] "
                    "[--since=YYYY-MM-DD] [--until=YYY-MM-DD]\n");
            exit(1);
        }
    }

    Reader reader;
    char readBuffer[65536];
    FileReadStream is(stdin, readBuffer, sizeof(readBuffer));

    bool ok;
    if (flags & FLAG_DUMP_IDS) {
        ItemIdsHandler<> handler;
        ok = reader.Parse<kParseValidateEncodingFlag>(is, handler);
    } else {
        ItemsHandler<> handler;
        if (idfile)
            handler.item_ids = readIdFile(idfile);
        printd(" item_ids size %zu\n", handler.item_ids.size());
        ok = reader.Parse<kParseValidateEncodingFlag>(is, handler);
    }

    if (!ok) {
        fprintf(stderr, "\nError(%u): %s\n",
                (unsigned)reader.GetErrorOffset(), reader.GetParseError());
        return 1;
    }

    for (auto &f : outputFiles)
        fclose(f.second);

    return 0;
}
