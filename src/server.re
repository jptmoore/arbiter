
open Lwt.Infix;

let rep_endpoint = ref("tcp://0.0.0.0:5555");
let router_endpoint = ref("tcp://0.0.0.0:5556");
let log_mode = ref(false);
let server_secret_key_file = ref("");
let server_secret_key = ref("");
let router_secret_key = ref("");
let router_public_key = ref("");
let token_secret_key = ref("");
let token_secret_key_file = ref("");
let databox = ref(false);

type t = {
  zmq_ctx: Protocol.Zest.t,
  state_ctx: State.t,
  hypercat_ctx: Hypercat.t,
  version: int
};

module Ack = {
  type t =
    | Code(int)
    | Payload(int, string)
};

let parse_cmdline = () => {
  let usage = "usage: " ++ Sys.argv[0];
  let speclist = [
    ("--request-endpoint", Arg.Set_string(rep_endpoint), ": to set the request/reply endpoint"),
    ("--enable-logging", Arg.Set(log_mode), ": turn debug mode on"),
    ("--databox", Arg.Set(databox), ": enable Databox mode"),
    ("--secret-key-file", Arg.Set_string(server_secret_key_file), ": to set the curve secret key"),
    ("--token-key-file", Arg.Set_string(token_secret_key_file), ": to set the token secret key")
    ];
    Arg.parse(speclist, (x) => raise(Arg.Bad("Bad argument : " ++ x)), usage);
};

let init = (zmq_ctx)  => {
  zmq_ctx: zmq_ctx,
  state_ctx: State.create(),
  hypercat_ctx: Hypercat.create(),
  version: 1
};

let data_from_file = (file) =>
  Fpath.v(file)
  |> Bos.OS.File.read
  |> (
    (result) =>
      switch result {
      | Rresult.Error(_) => failwith("failed to access file")
      | Rresult.Ok(key) => key
      }
  );

let set_server_key = (file) => 
  if (file != "") {server_secret_key := data_from_file(file)};

let set_token_key = (file) => 
  if (file != "") {token_secret_key := data_from_file(file)};


let enable_databox_mode = () => {
  server_secret_key := data_from_file("/run/secrets/ZMQ_SECRET_KEY"); 
  token_secret_key := data_from_file("/run/secrets/CM_KEY");
};



let setup_router_keys = () => {
  let (public_key, private_key) = ZMQ.Curve.keypair();
  router_secret_key := private_key;
  router_public_key := public_key;
};

let ack = (kind) =>
  Ack.(
    (
      switch kind {
      | Code(n) => Protocol.Zest.create_ack(n)
      | Payload(format, data) => Protocol.Zest.create_ack_payload(format, data)
      }
    )
    |> Lwt.return
  );

let unhandled_error = (e, ctx) => {
  let msg = Printexc.to_string(e);
  let stack = Printexc.get_backtrace();
  Logger.error_f("unhandled_error", Printf.sprintf("%s%s", msg, stack))
  >>= (() => ack(Ack.Code(160)) >>= ((resp) => Protocol.Zest.send(ctx.zmq_ctx, resp)));
};

let handle_options = (oc, bits) => {
  let options = Array.make(oc, (0, ""));
  let rec handle = (oc, bits) =>
    if (oc == 0) {
      bits;
    } else {
      let (number, value, r) = Protocol.Zest.handle_option(bits);
      let _ = Logger.debug_f("handle_options", Printf.sprintf("%d:%s", number, value));
      options[oc - 1] = (number, value);
      handle(oc - 1, r);
    };
  (options, handle(oc, bits));
};

let handle_get_status = (ctx, prov) => {
  ack(Ack.Code(65));
};

let create_uuid = () => Uuidm.v4_gen(Random.State.make_self_init(), ()) |> Uuidm.to_string;


let handle_get_store_secret = (ctx, prov) => {
  open Ezjsonm;
  let uri_host = Prov.uri_host(prov);
  if (State.exists(ctx.state_ctx, uri_host)) {
    let record = State.get(ctx.state_ctx, uri_host);
    let secret = create_uuid();
    let json = update(value(record), ["secret"], Some(string(secret)));
    let obj = `O(get_dict(json));
    State.replace(ctx.state_ctx, uri_host, obj);
    ack(Ack.Payload(0, secret));
  } else {
    ack(Ack.Code(129)); 
  }
};

let handle_get_cat = (ctx, prov) => {
  let json = Hypercat.get(ctx.hypercat_ctx);
  ack(Ack.Payload(50, Ezjsonm.to_string(json)));
};


let handle_get = (ctx, prov) => {
  let uri_path = Prov.uri_path(prov);
  let path_list = String.split_on_char('/', uri_path);
  switch path_list {
    | ["", "status"] => handle_get_status(ctx, prov);
    | ["", "store", "secret"] => handle_get_store_secret(ctx, prov);
    | ["", "cat"] => handle_get_cat(ctx, prov);
    | _ => ack(Ack.Code(128)); 
    };
};


let to_json = (payload) => {
  open Ezjsonm;
  let parsed =
    try (Some(from_string(payload))) {
    | Parse_error(_) => None
    };
  parsed;
};


let is_valid_token_data = (json) => {
  open Ezjsonm;
  mem(json, ["path"]) && mem(json, ["method"]) && mem(json, ["target"]); 
};



let mint_token = (path, meth, target, key) => {
  open Printf;
  let path = sprintf("path = %s", path);
  let meth = sprintf("method = %s", meth);
  let target = sprintf("target = %s", target);
  Mint.mint_token(~path=path, ~meth=meth, ~target=target, ~key=key, ());
};

let get_route = (record) => {
  open Ezjsonm;
  let arr = find(value(record), ["permissions"]);
  `A(get_list((x) => find(x,["route"]), arr));
};


let path_match = (s1, s2) => {
  String.(length(s1) <= length(s2) && s1 == sub(s2, 0, length(s1) - 1) ++ "*");
};


let route_exists_worker = (r1, r2) => {
  open Ezjsonm;
  let path = get_string(find(r1, ["path"]));
  if (Str.last_chars(path, 1) == "*") {
    let meth = find(r1, ["method"]);
    let meth' = find(r2, ["method"]);
    let target = find(r1, ["target"]);
    let target' = find(r2, ["target"]);
    let path' = get_string(find(r2, ["path"]));
    path_match(path,path') && meth == meth' && target == target'; 
  } else {
    r1 == r2;
  };
};

let route_exists = (record, route) => {
  open Ezjsonm;
  let arr = get_route(record);
  let lis = get_list((x) => x, arr);
  List.exists((x) => route_exists_worker(x,route)) (lis);
  /* List.exists((x) => x == route) (lis); */
};


let get_secret = (ctx, target) => {
  open Ezjsonm;
  if (State.exists(ctx.state_ctx, target)) {
    let record = State.get(ctx.state_ctx, target);
    get_string(find(value(record), ["secret"]));
  } else {
    "";
  }
};

let handle_token = (ctx, prov, json) => {
  open Ezjsonm;
  let uri_host = Prov.uri_host(prov);
  if (State.exists(ctx.state_ctx, uri_host)) {
    let record = State.get(ctx.state_ctx, uri_host);
    let permissions = find(value(record), ["permissions"]);
    let target = get_string(find(json, ["target"]));
    let secret = get_secret(ctx, target);
    if (permissions != dict([]) && secret != "") {
      if (route_exists(record, json)) {
        let path = get_string(find(json, ["path"]));
        let meth = get_string(find(json, ["method"]));
        let token = mint_token(path, meth, target, secret);
        ack(Ack.Payload(0,token));
      } else {
        ack(Ack.Code(129));
      }
    } else {
      ack(Ack.Code(129));
    };
  } else {
    ack(Ack.Code(129));
  };
};

let handle_post_token = (ctx, prov, payload) => {
  switch (to_json(payload)) {
    | Some(json) => is_valid_token_data(json) ? handle_token(ctx,prov,json) : ack(Ack.Code(128)); 
    | None => ack(Ack.Code(128)); 
    };
};

let is_valid_upsert_container_info_data = (json) => {
  open Ezjsonm;
  mem(json, ["name"]) && mem(json, ["type"]) && mem(json, ["key"]); 
};

let upsert_container_info = (ctx, prov, json) => {
  open Ezjsonm;
  let name = get_string(find(json, ["name"]));
  Hypercat.add(ctx.hypercat_ctx, name);
  let json' = update(json, ["permissions"], Some(`A([])));
  let json'' = update(json', ["secret"], Some(string("")));
  let obj = `O(get_dict(json''));
  State.add(ctx.state_ctx, name, obj);
  let _ = Logger.info_f("upsert_container_info", to_string(obj));
  ack(Ack.Payload(0,name));
};

let handle_post_upsert_container_info = (ctx, prov, payload) => {
  switch (to_json(payload)) {
    | Some(json) => is_valid_upsert_container_info_data(json) ? upsert_container_info(ctx, prov, json) : ack(Ack.Code(128)); 
    | None => ack(Ack.Code(128)); 
    };
};

let is_valid_delete_container_info_data = (json) => {
  open Ezjsonm;
  mem(json, ["name"]); 
};

let delete_container_info = (ctx, prov, json) => {
  open Ezjsonm;
  let name = get_string(find(json, ["name"]));
  Hypercat.remove(ctx.hypercat_ctx, name);
  State.remove(ctx.state_ctx, name);
  ack(Ack.Code(66));
};


let handle_post_delete_container_info = (ctx, prov, payload) => {
  switch (to_json(payload)) {
    | Some(json) => is_valid_delete_container_info_data(json) ? delete_container_info(ctx,prov,json) : ack(Ack.Code(128)); 
    | None => ack(Ack.Code(128)); 
    };
};

let is_valid_container_permissions_data = (ctx, json) => {
  open Ezjsonm;
  mem(json, ["name"]) && 
  mem(json, ["route", "target"]) && 
  mem(json, ["route", "path"]) && 
  mem(json, ["route", "method"]) && 
  mem(json, ["caveats"]);
};





let add_permissions = (record, item) => {
  open Ezjsonm;
  let json = find(value(record), ["permissions"]);
  let lis = get_list((x) => x, json);
  let lis' = List.append(lis, [item]);
  list((x) => x, lis');
};

let grant_container_permissions = (ctx, prov, json) => {
  open Ezjsonm;
  let name = get_string(find(json, ["name"]));
  if (State.exists(ctx.state_ctx, name)) {
    let record = State.get(ctx.state_ctx, name);
    let record' = update(value(record), ["permissions"], Some(add_permissions(record, json)));
    let obj = `O(get_dict(record'));
    State.replace(ctx.state_ctx, name, obj);
    let _ = Logger.info_f("grant_container_permissions", to_string(obj));
    let arr = `A(get_list((x) => x, get_route(obj)));
    ack(Ack.Payload(50,to_string(arr)));
  } else {
    ack(Ack.Code(129))
  };
};

let remove_permissions = (record, item) => {
  open Ezjsonm;
  let json = find(value(record), ["permissions"]);
  let lis = get_list((x) => x, json);
  let route = find(item, ["route"]);
  let lis' = List.filter((x) => (find(x, ["route"]) != route), lis);
  list((x) => x, lis');
};

let revoke_container_permissions = (ctx, prov, json) => {
  open Ezjsonm;
  let name = get_string(find(json, ["name"]));
  if (State.exists(ctx.state_ctx, name)) {
    let record = State.get(ctx.state_ctx, name);
    let record' = update(value(record), ["permissions"], Some(remove_permissions(record, json)));
    let record'' = update(record', ["secret"], Some(string("")));
    let obj = `O(get_dict(record''));
    State.replace(ctx.state_ctx, name, obj);
    let _ = Logger.info_f("revoke_container_permissions", to_string(obj));
    let arr = `A(get_list((x) => x, get_route(obj)));
    ack(Ack.Payload(50,to_string(arr)));
  } else {
    ack(Ack.Code(129))
  };
};


let handle_post_grant_container_permissions = (ctx, prov, payload) => {
  switch (to_json(payload)) {
    | Some(json) => is_valid_container_permissions_data(ctx,json) ? grant_container_permissions(ctx,prov,json) : ack(Ack.Code(128)); 
    | None => ack(Ack.Code(128)); 
    };
};


let handle_post_revoke_container_permissions = (ctx, prov, payload) => {
  switch (to_json(payload)) {
    | Some(json) => is_valid_container_permissions_data(ctx,json) ? revoke_container_permissions(ctx,prov,json) : ack(Ack.Code(128)); 
    | None => ack(Ack.Code(128)); 
    };
};

let handle_post = (ctx, prov, payload) => {
  let uri_path = Prov.uri_path(prov);
  let path_list = String.split_on_char('/', uri_path);
  switch path_list {
    | ["", "token"] => handle_post_token(ctx, prov, payload);
    | ["", "cm", "upsert-container-info"] => handle_post_upsert_container_info(ctx, prov, payload);
    | ["", "cm", "delete-container-info"] => handle_post_delete_container_info(ctx, prov, payload);
    | ["", "cm", "grant-container-permissions"] => handle_post_grant_container_permissions(ctx, prov, payload);
    | ["", "cm", "revoke-container-permissions"] => handle_post_revoke_container_permissions(ctx, prov, payload);
    | _ => ack(Ack.Code(128)); 
    };
};


let is_valid_uri_host = (ctx, uri_host, token) => {
  open Ezjsonm;
  if (State.exists(ctx.state_ctx, uri_host)) {
    let record = State.get(ctx.state_ctx, uri_host);
    let key = get_string(find(value(record), ["key"]));
    key == token;
  } else {
    false;
  }
};


let is_valid_token = (ctx, prov) => {
  let token = Prov.token(prov);
  let uri_host = Prov.uri_host(prov);
  switch token_secret_key^ {
  | "" => true
  | _ => (token == token_secret_key^) || (is_valid_uri_host(ctx,uri_host,token));
  };
};


let handle_msg = (msg, ctx) => {
  Logger.debug_f("handle_msg", Printf.sprintf("Received:\n%s", msg)) >>= (() => {
    let r0 = Bitstring.bitstring_of_string(msg);
    let (tkl, oc, code, r1) = Protocol.Zest.handle_header(r0);
    let (token, r2) = Protocol.Zest.handle_token(r1, tkl);
    let (options, r3) = handle_options(oc, r2);
    let prov = Prov.create(~code=code, ~options=options, ~token=token);
    if (is_valid_token(ctx, prov)) {
      let payload = Bitstring.string_of_bitstring(r3);
      switch code {
      | 1 => handle_get(ctx, prov);
      | 2 => handle_post(ctx, prov, payload);
      | _ => ack(Ack.Code(128));
      };
    } else {
      ack(Ack.Code(129)); 
    };
  });
};

let server = (ctx) => {
  open Logger;
  let rec loop = () =>
    Protocol.Zest.recv(ctx.zmq_ctx) >>= ((msg) =>
      handle_msg(msg, ctx) >>= ((resp) =>
        Protocol.Zest.send(ctx.zmq_ctx, resp) >>= (() =>
          Logger.debug_f("server", Printf.sprintf("Sending:\n%s", resp)) >>= (() => 
            loop()))));
  Logger.info_f("server", "active") >>= (() => loop());
};

let terminate_server = (ctx, m) => {
  Lwt_io.printf("Shutting down server...\n")
    >>= (() => Protocol.Zest.close(ctx.zmq_ctx) |> (() => exit(0)));
};

let rec run_server = (ctx) => {
  let _ =
    try (Lwt_main.run(server(ctx))) {
    | e => unhandled_error(e, ctx)
    };
  run_server(ctx);
};

let setup_server = () => {
  parse_cmdline();
  log_mode^ ? Logger.init () : ();
  setup_router_keys();
  set_server_key(server_secret_key_file^);
  set_token_key(token_secret_key_file^);
  databox^ ? enable_databox_mode () : ();
  let zmq_ctx =
    Protocol.Zest.create(
      ~endpoints=(rep_endpoint^, router_endpoint^),
      ~keys=(server_secret_key^, router_secret_key^)
    );
  let ctx = init(zmq_ctx);
  run_server(ctx) |> (() => terminate_server(ctx));
};

setup_server();
