# hn2mbox

After reading about
[how to download all of Hacker News](https://news.ycombinator.com/item?id=7835605)
I was wondering if I could (ab)use my email client to browse the data offline.
The result is `hn2mbox`, a tool that converts Hacker News stories and comments
from JSON to mbox format. The input files can be obtained from
[GitHub](https://github.com/sytelus/HackerNewsData).

## Caveats

The generated mboxes are huge. Putting all data in a single mbox is impractical
with the usual amount of RAM (4 to 8 GB). Even when data is splitted yearly some
mboxes contain a couple of millions of messages (Hacker News exponential growth
for the win!).

On the other hand, some short tests with Thunderbird show that, as long as the
mbox index file is in RAM, the program remains quite responsive and an email
client offers an acceptable interface to search, filter and tag comments and
stories.

## Hacker News Headers

`hn2mbox` adds some headers with Hacker News information to each message in
the mbox. For comments they look as follows:

    X-HackerNews-Link: https://news.ycombinator.com/item?id=15
    X-HackerNews-Points: 5
    X-HackerNews-Story-Link: https://news.ycombinator.com/item?id=1

And for stories something like this:

    X-HackerNews-Link: https://news.ycombinator.com/item?id=1
    X-HackerNews-Points: 57
    X-HackerNews-Url: http://ycombinator.com
    X-HackerNews-Num-Comments: 18

## How to generate the mboxes

1. build the `hn2mbox` tool

        cd ~/hn2mbox; make

1. Download and unpack the data dump. I had more luck with the copy at the
   [internet archive](https://archive.org/details/HackerNewsStoriesAndCommentsDump)
   than with the [torrent](https://github.com/sytelus/HackerNewsData) at GitHub.

1. Dump the parent ids of stories and comments to a file. We will need them later
   to thread the messages together in the mbox files.

        cd ~/HackerNews
        ~/hn2mbox/hn2mbox --dump-ids < HNStoriesAll.json > ids.txt
        ~/hn2mbox/hn2mbox --dump-ids < HNCommentsAll.json >> ids.txt

1. Convert the data to mbox format. To prevent the email client from choking on
   multi GB files, split each month's data in a file. Output files are named as
   HN-yyyy-mm

        ~/hn2mbox/hn2mbox --split < HNStoriesAll.json
        ~/hn2mbox/hn2mbox --id-file=ids.txt --split < HNCommentsAll.json

1. Now, a mbox per month is too cumbersome; as a trade-off put each year of data
   in a file. (Depending on the amount of RAM avaliable you may want to arrange
   the files differently).

        for i in $(seq 2006 2014); do
            cat HN-$i-* > HN-$i
        done

1. Finally, import the mboxes in your email client. For Thunderbird, just quit
   the program and copy the files to your profile. When you start Thunderbird
   again, it will import them (but see below):

        for i in $(seq 2006 2014); do
            mv HN-$i ~/.thunderbird/*.default/Mail/Local\ Folders
        done

If you are only interested in the stories or comments of a certain period,
you can specify it with the `--since` and `--until` options

## Thunderbird Tips

Thunderbird isn't actualy conceived to manage multi GByte mail boxes.
Nonetheless, with a little help it keeps up to the task quite decently.

### Disable global index

Go to
Preferences > Advanced > Advanced Configuration and uncheck
"Enable Global Search and Indexer".

### Reduce the number of index files opened simultaneously

See <http://kb.mozillazine.org/Limits_-_Thunderbird#Open_.msf_files>
and <http://kb.mozillazine.org/Mail_and_news_settings>.

For each mbox file that you open, Thunderbird loads into memory its .msf index.
For an imported Hacker News mbox, this .msf file can take several hundred
MBytes. You might want to limit the number of .msf files that thunderbird keeps
open simultaneously. Go to Preferences > Advanced > General > Config Editor and
change `mail.db.max_open` to 2 (or perhaps 3) and maybe `mail.db.idle_limit`
(which defaults to 5 min).

### CompactHeader extension

I found the
[CompactHeader](https://addons.mozilla.org/de/thunderbird/addon/compactheader/)
extension quite helpful to quickly toggle the header visivility.

## Acknowledgemnts

* [rapidjson](https://github.com/pah/rapidjson# pah/rapidjson) is used to parse
the input files.

## License

`hn2mbox` is distributed under the
[MIT license](http://opensource.org/licenses/MIT).
