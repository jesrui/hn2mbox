/**
 * hn2mbox -- convert HackerNews stories and comments from JSON to mbox format
 *
 * See https://github.com/sytelus/HackerNewsData
 */

#include <algorithm>
#include <cerrno>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
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

enum {
    FLAG_SPLIT         =  1 << 1,
};

int flags;
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
    string objectID;
};

typedef unordered_map <int, FILE*> Files;
Files outputFiles;
FILE *getFile(struct tm *date)
{
    if (!(flags & FLAG_SPLIT))
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

void dumpItemAsEmail(const Item &item)
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
            "Message-ID: <%s@hndump>\n"
            "From: %s <%s@hndump>\n"
            "Subject: %s\n"
            "Date: %s\n"
            "Mime-Version: 1.0\n"
            "Content-Type: text/html; charset=utf-8\n",
            item.objectID.c_str(),
            item.author.c_str(), item.author.c_str(),
            // FIXME: some items have neither title nor story_title
            item.title.empty() ? item.story_title.c_str() : item.title.c_str(),
            datestr);

    if (item.parent_id) { // this item is a comment
        fprintf(out, "In-Reply-To: <%u@hndump>\n", item.parent_id);
        fprintf(out, "References: <%u@hndump>\n", item.story_id);
    }
    fprintf(out, "X-HackerNews-Link: <https://news.ycombinator.com/item?id=%s>\n",
            item.objectID.c_str());
    fprintf(out, "X-HackerNews-Points: %d\n", item.points);
    if (!item.url.empty())
        fprintf(out, "X-HackerNews-Url: <%s>\n", item.url.c_str());
    if (item.story_id)
        fprintf(out, "X-HackerNews-Story-Link: "
                "<https://news.ycombinator.com/item?id=%u>\n", item.story_id);
    if (item.parent_id == 0) // this item is a story
        fprintf(out, "X-HackerNews-Num-Comments: %u\n", item.num_comments);

    // FIXME: We're cheating here because, according to RFC 5332, lines
    // should not be longer than 998 chars. But if we fix that by splitting
    // long lines, then we should escape lines starting with "From "...
    fprintf(out, "\n"
            "<html>%s</html>\n"
            "\n",
            item.story_text.empty() ? item.comment_text.c_str() : item.story_text.c_str());
}

template<typename Encoding = UTF8<>>
struct ItemsHandler {
    typedef typename Encoding::Ch Ch;

    ItemsHandler() : element(none), parent(noparent), level(0), item {} {}

    void Default() {}
    void Null() { element = none; }
    void Bool(bool) { Default(); }
    void Int(int i) { Int64(i); }
    void Uint(unsigned i) { Int64(i); }
    void Int64(int64_t i)
    {
        if (parent == _highlightResult)
            return;
        switch (element) {
        case points:        item.points = i;        break;
        case num_comments:  item.num_comments = i;  break;
        case story_id:      item.story_id = i;      break;
        case parent_id:     item.parent_id = i;     break;
        case created_at_i:  item.created_at_i = i;  break;
        default:            break;
        }

        element = none;
    }
    void Uint64(uint64_t) { Default(); }
    void Double(double) { Default(); }

    void String(const Ch* str, SizeType length, bool copy)
    {
        if (parent == _highlightResult)
            return;
        if (element == none) {
            if (strcmp("hits", str) == 0)
                parent = hits;
            else if (strcmp("_highlightResult", str) == 0)
                parent = _highlightResult;
            else if (strcmp("created_at", str) == 0)
                element = created_at;
            else if (strcmp("title", str) == 0)
                element = title;
            else if (strcmp("url", str) == 0)
                element = url;
            else if (strcmp("author", str) == 0)
                element = author;
            else if (strcmp("points", str) == 0)
                element = points;
            else if (strcmp("story_text", str) == 0)
                element = story_text;
            else if (strcmp("comment_text", str) == 0)
                element = comment_text;
            else if (strcmp("num_comments", str) == 0)
                element = num_comments;
            else if (strcmp("story_id", str) == 0)
                element = story_id;
            else if (strcmp("story_title", str) == 0)
                element = story_title;
            else if (strcmp("story_url", str) == 0)
                element = story_url;
            else if (strcmp("parent_id", str) == 0)
                element = parent_id;
            else if (strcmp("created_at_i", str) == 0)
                element = created_at_i;
            else if (strcmp("objectID", str) == 0)
                element = objectID;
            return;
        }

        if (element == none)
            return;

        // minimal string normalization
        string s = str;
        replace(s.begin(), s.end(), '\n', ' ');
        remove(s.begin(), s.end(), '\r');

        switch (element) {
        case created_at:    item.created_at = s;    break;
        case title:         item.title = s;         break;
        case url:           item.url = s;           break;
        case author:        item.author = s;        break;
        case story_text:    item.story_text = s;    break;
        case comment_text:  item.comment_text = s;  break;
        case story_title:   item.story_title = s;   break;
        case story_url:     item.story_url = s;     break;
        case objectID:      item.objectID = s;      break;
        default:            break;
        }

        element = none;
    }
    void StartObject() { ++level; }
    void EndObject(SizeType)
    {
        if (--level == 1) {
            parent = hits;
            dumpItemAsEmail(item);
            item = {};
        }
    }
    void StartArray() { Default(); }
    void EndArray(SizeType) { Default(); }

    enum {
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
    } element;
    enum {
        noparent,
        hits,
        _highlightResult,
    } parent;
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
        { "since",   required_argument,   NULL,   's' },
        { "until",   required_argument,   NULL,   'u' },
        { "split",   0,   NULL,   'S' },
        { NULL,      0,                   NULL,   0 }
    };

    int opt;
    int err;
    while ((opt = getopt_long(argc, argv, "s:u:",
                              long_options, NULL)) != EOF) {
        switch (opt) {
        case 'S':
            flags |= FLAG_SPLIT;
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
            fprintf(stderr, "usage: hn2mbox [--split] [--since=YYYY-MM-DD] [--until=YYY-MM-DD]\n");
            exit(1);
        }
    }

    Reader reader;
    char readBuffer[65536];
    FileReadStream is(stdin, readBuffer, sizeof(readBuffer));

    ItemsHandler<> handler;

    if (!reader.Parse<kParseValidateEncodingFlag>(is, handler)) {
        fprintf(stderr, "\nError(%u): %s\n",
                (unsigned)reader.GetErrorOffset(), reader.GetParseError());
        return 1;
    }

    for (auto &f : outputFiles)
        fclose(f.second);

    return 0;
}
