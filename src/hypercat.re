open Lwt.Infix;
open Ezjsonm;

let tcp_port = "5555";

type t = {mutable cat: Ezjsonm.t};
  
let create = () => {
  cat: from_channel(open_in("base-cat.json"))
};

let get = (ctx) => {
  ctx.cat;
};


let make_href = (name, port) => {
  Printf.sprintf("tcp://%s:%s", name, port);
};

let make_item = (name) => {
  let href = make_href(name, tcp_port);
  `O([ 
    ("href", string(href)),
    ("item-metadata", 
      `A([
          `O([
            ("rel", string("urn:X-hypercat:rels:hasDescription:en")), 
            ("val", string(href))]), 
          `O([
            ("rel", string("urn:X-hypercat:rels:isContentType")), 
            ("val", string("application/vnd.hypercat.catalogue+json"))])]))])
};

let update = (ctx, name) => {
  let items = find(value(ctx.cat), ["items"]);
  let item = make_item(name);
  let lis = get_list((x) => x, items);
  let lis' = List.append(lis, [item]);
  let items' = list((x) => x, lis');
  let cat = update((value(ctx.cat)), ["items"], Some(items'));
  ctx.cat = `O(get_dict(cat));
};