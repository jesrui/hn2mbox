/**
 * hn2mbox -- convert HackerNews stories and comments from JSON to mbox format
 *
 * See https://github.com/sytelus/HackerNewsData
 */

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "rapidjson/reader.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"

#define printd(...) fprintf(stderr, __VA_ARGS__)
//#define printd(...)

using namespace std;
using namespace rapidjson;

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

void dumpItemAsEmail(const Item &item)
{
    char datetime[100];
    time_t dt = item.created_at_i;
    if (!strftime(datetime, sizeof datetime, "%a, %d %b %Y %T %z", gmtime(&dt)))
        *datetime = '\0';

    printf("From \n"
           "Message-ID: <%s@hndump>\n"
           "From: %s <%s@hndump>\n"
           "Subject: %s\n"
           "Date: %s\n"
           "Mime-Version: 1.0\n"
           "Content-Type: text/html; charset=utf-8\n",
           item.objectID.c_str(),
           item.author.c_str(), item.author.c_str(),
           item.title.empty() ? item.story_title.c_str() : item.title.c_str(),
           datetime);

    if (item.parent_id)
        printf("In-Reply-To: <%u@hndump>\n", item.parent_id);

    printf("X-HackerNews-Link: <https://news.ycombinator.com/item?id=%s>\n",
           item.objectID.c_str());
    printf("X-HackerNews-Points: %d\n", item.points);
    if (!item.url.empty())
        printf("X-HackerNews-Url: <%s>\n", item.url.c_str());
    if (item.story_id)
        printf("X-HackerNews-Story-Link: "
               "<https://news.ycombinator.com/item?id=%u>\n", item.story_id);
    if (item.story_id == 0)
        printf("X-HackerNews-Num-Comments: %u\n", item.num_comments);

    // FIXME: We're cheating here because, according to RFC 5332, lines
    // should not be longer than 998 chars. But if we fix that by splitting
    // long lines, then we should escape lines starting with "From "...
    printf("\n"
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
        switch (element) {
        case created_at:    item.created_at = str;    break;
        case title:         item.title = str;         break;
        case url:           item.url = str;           break;
        case author:        item.author = str;        break;
        case story_text:    item.story_text = str;    break;
        case comment_text:  item.comment_text = str;  break;
        case story_title:   item.story_title = str;   break;
        case story_url:     item.story_url = str;     break;
        case objectID:      item.objectID = str;      break;
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

int main(int, char*[])
{
    Reader reader;
    char readBuffer[65536];
    FileReadStream is(stdin, readBuffer, sizeof(readBuffer));

    ItemsHandler<> handler;

    if (!reader.Parse<kParseValidateEncodingFlag>(is, handler)) {
        fprintf(stderr, "\nError(%u): %s\n",
                (unsigned)reader.GetErrorOffset(), reader.GetParseError());
        return 1;
    }

    return 0;
}
