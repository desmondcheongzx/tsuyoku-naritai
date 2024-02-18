# Browser

2 years ago [Nick](https://n-young.me/) shared the [Web Browser Engineering book](https://browser.engineering/index.html) with me. I never got around to working through it, but what better time than now.

Much of the code here follows the book by Pavel Panchekha and Chris Harrelson. The solutions to the additional exercises are my own.

## Usage

```
python3 main.py <usage>
```

Currently this programs is able to use the `http` and `https` schemes with HTTP/1.0 or HTTP/1.1, and the `file` scheme for local files. For example

```
python3 main.py https://example.org:443/index.htm
```
