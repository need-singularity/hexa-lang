# `ckpt/mod`

_re-export surface for the proof-carrying ckpt module_

**Source:** [`stdlib/ckpt/mod.hexa`](../../stdlib/ckpt/mod.hexa)  

## Overview

 stdlib/ckpt/mod.hexa — re-export surface for the proof-carrying ckpt module

 The .pcc (proof-carrying checkpoint) format embeds a meta2 cert alongside
 chunked payload, bound by an xxh64-leaf Merkle tree. Importing this mod
 is equivalent to importing the four concrete submodules below.

 TYPICAL USAGE
     use "stdlib/ckpt/mod"
     let cert_json = emit_cert(my_meta2_cert)        // from meta2_validator
     write_pcc("out.pcc", payload_bytes, cert_json, #{})
     let v = verify_pcc("out.pcc")
     if !v["ok"] { eprintln("tampered: " + v["code"] + " — " + v["reason"]) }

_No public functions detected._

---

← [Back to stdlib index](README.md)
