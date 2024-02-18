import socket
import ssl


class URL:
    def __init__(self, url: str) -> None:
        self._setUrl(url)

    def _setUrl(self, url: str) -> None:
        self.scheme, url = url.split("://", 1)
        assert self.scheme in ["http", "https", "file"]
        if self.scheme == "http":
            self.port = 80
        elif self.scheme == "https":
            self.port = 443

        if self.scheme == "file":
            self.path = "/" + url
            return

        if "/" not in url:
            url += "/"
        self.host, url = url.split("/", 1)
        if ":" in self.host:
            self.host, port = self.host.split(":", 1)
            self.port = int(port)
        self.path = "/" + url

    def _updateUrl(self, url: str) -> None:
        if len(url) > 0:
            if url[0] == "/":
                # New url does not contain host and scheme, so we reuse the
                # existing values.
                self.path = url
            else:
                # Use our standard url setter.
                self._setUrl(url)

    def _getHTTPResponse(self, headers={}, max_redirects=5) -> str:
        sock = socket.socket(
            family=socket.AF_INET,
            type=socket.SOCK_STREAM,
            proto=socket.IPPROTO_TCP
        )
        sock.connect((self.host, self.port))
        if self.scheme == "https":
            ctx = ssl.create_default_context()
            sock = ctx.wrap_socket(sock, server_hostname=self.host)
        request_string = f"GET {self.path} HTTP/1.0\r\nHOST: {self.host}\r\n"
        for header in headers:
            request_string += f"{header}: {headers[header]}\r\n"
        request_string += "\r\n"
        sock.send(request_string.encode("utf-8"))
        response = sock.makefile("r", encoding="utf-8", newline="\r\n")
        statusline = response.readline()
        version, status_string, explanation = statusline.split(" ", 2)
        status = int(status_string)
        response_headers = {}
        while True:
            line = response.readline()
            if line == "\r\n":
                break
            header, value = line.split(":", 1)
            response_headers[header.casefold()] = value.strip()

        assert "transfer-encoding" not in response_headers
        assert "content-encoding" not in response_headers

        # Handle potential redirect.
        if 300 <= status and status < 400 and max_redirects > 0:
            if "location" in response_headers:
                sock.close()
                self._updateUrl(response_headers["location"])
                return self._getHTTPResponse(headers, max_redirects - 1)

        body = response.read()
        sock.close()
        return body

    def _getFileResponse(self) -> str:
        with open(self.path, 'r') as file:
            file_contents = file.read()
        return file_contents

    def request(self, headers={}) -> str:
        if self.scheme in ["http", "https"]:
            return self._getHTTPResponse(headers)
        return self._getFileResponse()


def show(body: str) -> None:
    in_tag = False
    for c in body:
        if c == "<":
            in_tag = True
        elif c == ">":
            in_tag = False
        elif not in_tag:
            print(c, end="")


def load(url: URL) -> None:
    body = url.request(headers={
        "CONNECTION": "HTTP/1.1", "User-Agent": "SanityStudio/1.0"})
    show(body)


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        load(URL("file://Users/desmond.cheong/tsuyoku-naritai/browser/tmp"))
    else:
        load(URL(sys.argv[1]))
