### Introduction

A server that can mint tokens to be used by [ZestDB](https://me-box.github.io/zestdb/).


### Basic usage examples

You can run a server and test client using [Docker](https://www.docker.com/). Each command supports --help to get a list of parameters.

#### starting server

```bash
$ docker run -p 5555:5555 -p 5556:5556 -d --name arbiter --rm jptmoore/arbiter /app/zest/server.exe --secret-key-file example-server-key
```

You can also start the server using the ```--token-key-file``` which will then require the client to pass the same string in the file as a token for authorisation on each request.

#### running client to generate a token

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<'  --path '/token' --mode post --payload '{"path": "/ts/foo", "method": "POST", "target": "localhost", "key": "secret"}'
```

This will generate a token that allows you to POST data using ZestDB to a time series called 'foo' on a host called 'localhost' with 'secret' as the token key. If the server requires authorisation supply the string using the ```--token``` flag.

### API

#### Generate token
    URL: /token
    Method: POST
    Parameters: JSON body of data contains macaroon caveats and key
    Notes: generates an access token for ZestDB

