
let rep_endpoint = ref("tcp://0.0.0.0:5555");
let router_endpoint = ref("tcp://0.0.0.0:5556");
let log_mode = ref(false);
let server_secret_key_file = ref("");
let server_secret_key = ref("");
let router_secret_key = ref("");

type t = {
  zmq_ctx: Protocol.Zest.t,
  version: int
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

let setup_server = () => {
  parse_cmdline();
  set_server_key(server_secret_key_file^);
  let zmq_ctx =
    Protocol.Zest.create(
      ~endpoints=(rep_endpoint^, router_endpoint^),
      ~keys=(server_secret_key^, router_secret_key^)
    );
  let ctx = init(zmq_ctx);
};

setup_server();