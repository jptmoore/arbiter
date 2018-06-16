open Lwt.Infix;
open Ezjsonm;

type t = {
  cat: Ezjsonm.t
};
  
let create = () => {
  cat: Ezjsonm.from_channel(open_in("base-cat.json"))
};

let get = (~ctx) => {
  ctx.cat;
};

