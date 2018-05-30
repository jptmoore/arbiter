
open Lwt.Infix;

let rep_endpoint = ref("tcp://0.0.0.0:5555");
let router_endpoint = ref("tcp://0.0.0.0:5556");
let log_mode = ref(false);
let server_secret_key_file = ref("");
let server_secret_key = ref("");
let router_secret_key = ref("");
let router_public_key = ref("");

type t = {
  zmq_ctx: Protocol.Zest.t,
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
    ("--secret-key-file", Arg.Set_string(server_secret_key_file), ": to set the curve secret key"),
    ];
    Arg.parse(speclist, (x) => raise(Arg.Bad("Bad argument : " ++ x)), usage);
};

let init = (zmq_ctx)  => {
  zmq_ctx: zmq_ctx,
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

let set_server_key = (file) => server_secret_key := data_from_file(file);

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

let handle_get = (ctx, prov) => {
  ack(Ack.Payload(69,"w00t!"));
};


let handle_post = (ctx, prov, payload) => {
  let uri_path = Prov.uri_path(prov);
  let path_list = String.split_on_char('/', uri_path);
  let macaroon = Mint.mint_token(~path="", ~meth="", ~target="", ~key="", ());
  switch path_list {
  | ["", "token"] => ack(Ack.Payload(69,macaroon));
  | _ => ack(Ack.Code(128)); 
  };
  
};

let handle_msg = (msg, ctx) =>
  Logger.debug_f("handle_msg", Printf.sprintf("Received:\n%s", msg))
  >>= (
    () => {
      let r0 = Bitstring.bitstring_of_string(msg);
      let (tkl, oc, code, r1) = Protocol.Zest.handle_header(r0);
      let (token, r2) = Protocol.Zest.handle_token(r1, tkl);
      let (options, r3) = handle_options(oc, r2);
      let payload = Bitstring.string_of_bitstring(r3);
      let prov = Prov.create(~code=code, ~options=options, ~token=token);
      switch code {
      | 1 => handle_get(ctx, prov);
      | 2 => handle_post(ctx, prov, payload);
      | _ => failwith("invalid code")
      };
    }
  );

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
  let zmq_ctx =
    Protocol.Zest.create(
      ~endpoints=(rep_endpoint^, router_endpoint^),
      ~keys=(server_secret_key^, router_secret_key^)
    );
  let ctx = init(zmq_ctx);
  run_server(ctx) |> (() => terminate_server(ctx));
};

setup_server();