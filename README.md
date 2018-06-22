### Introduction

A server that can mint tokens to be used by [ZestDB](https://me-box.github.io/zestdb/).


### Basic usage

We will demonstrate the sequence required for generating a token capable of being used by a Databox App that wishes to get data from a Databox store.

You can run a server and test client using [Docker](https://www.docker.com/). Each command supports --help to get a list of parameters.

#### starting server

```bash
$ docker run -p 5555:5555 -p 5556:5556 -d --name arbiter --rm jptmoore/arbiter /app/zest/server.exe --secret-key-file example-server-key --token-key-file example-token-key
```

#### running client to register an App with arbiter

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<' --path '/cm/upsert-container-info' --mode post --payload '{"name": "foo", "type": "app", "key": "foosecret"}' --token secret
```

This will register an App called 'foo' with the arbiter that has an access key of 'foosecret'.

#### running client to register a Store with arbiter

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<' --path '/cm/upsert-container-info' --mode post --payload '{"name": "bar", "type": "store", "key": "barsecret"}' --token secret
```

This will register a Store called 'bar' with the arbiter that has an access key of 'barsecret'.

#### running client to grant permissions to an App

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<' --path '/cm/grant-container-permissions' --mode post --payload '{"name": "foo", "caveats": [], "route": {"method": "GET", "path": "/ts/sensor/*", "target": "bar"}}' --token secret
```

This will grant permissions to an App called 'foo' so that it is able to 'GET' data from a path that begins with '/ts/sensor' on a store called 'bar'.

#### running client to get token secret for Store

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<' --path '/store/secret' --mode get --identity bar --token barsecret
```

This will allow a Store called 'bar' to retrieve a secret used to verify tokens from App's and Drivers. The 'identity' flag is used here to set the 'uri_host' option in the Zest protocol. Without this flag the Unix hostname will be supplied in the GET request.

#### running client to generate token for App

```bash
$ docker run --network host -it jptmoore/zestdb /app/zest/client.exe --server-key 'vl6wu0A@XP?}Or/&BR#LSxn>A+}L)p44/W[wXL3<' --path '/token' --mode post --payload '{"method": "GET", "path": "/ts/sensor/latest", "target": "bar"}' --identity foo --token foosecret
```

This will generate an access token for an App called 'foo' that has permissions to be spent by a Store called 'bar' provided the exact permissions have been previously granted and a secret has also be generated. The 'identity' flag is used here to set the 'uri_host' option in the Zest protocol. Without this flag the Unix hostname will be supplied in the POST request.

### API

#### Status request
    URL: /status
    Method: GET
    Parameters:
    Notes: Check the server is up
    
    
#### Register with arbiter
    URL: /cm/upsert-container-info
    Method: POST
    Parameters: JSON dictionary of 'name', 'type' and 'target'
    Notes: Register an app/driver/store with arbiter    
     

#### Remove from arbiter
    URL: /cm/delete-container-info
    Method: POST
    Parameters: JSON dictionary of 'name'
    Notes: Remove app/driver/store from arbiter


#### Grant perimissions
    URL: /cm/grant-container-permissions
    Method: POST
    Parameters: JSON dictionary of 'name', 'route' and 'caveats' where route is dictionary of 'name', 'type' and 'target' and 'caveats' is an empty array ('caveats' is currently not used)
    Notes: Add permissions to an existing app or driver
    

#### Revoke permissions
    URL: /cm/revoke-container-permissions
    Method: POST
    Parameters: JSON dictionary of 'name', 'route' and 'caveats' where route is dictionary of 'name', 'type' and 'target' and 'caveats' is an empty array ('caveats' is currently not used)
    Notes: Revoke permissions from an existing app or driver
    
         
#### Generate token secret
    URL: /store/secret
    Method: GET
    Parameters: 
    Notes: generates a secret that is used by a store for verifying access tokens
    

#### Generate token
    URL: /token
    Method: POST
    Parameters: JSON dictionary of 'method', 'path' and 'target'
    Notes: generates an access token for an app/driver to be spent at a store
    
    
#### Hypercat
    URL: /cat
    Method: GET
    Parameters: 
    Notes: retrieves the hypercat
