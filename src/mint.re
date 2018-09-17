module Macaroon = Sodium_macaroons;

let mint_token = (~id="", ~location="", ~path, ~meth, ~target, ~key, ~observe, ()) => {
  let m = Macaroon.create(~id=id, ~location=location, ~key=key);
  let m = Macaroon.add_first_party_caveat(m, path);
  let m = Macaroon.add_first_party_caveat(m, meth);
  let m = Macaroon.add_first_party_caveat(m, target);
  let m = switch (observe) {
    | Some(observe) => Macaroon.add_first_party_caveat(m, observe);
    | _ => m;
    };
  Macaroon.serialize(m);
};
