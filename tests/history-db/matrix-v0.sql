BEGIN TRANSACTION;
CREATE TABLE chatty_im(
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  timestamp INTEGER NOT_NULL,
  direction INTEGER NOT NULL,
  account TEXT NOT_NULL,
  who TEXT NOT_NULL,
  uid TEXT NOT_NULL,
  message TEXT,
  UNIQUE (timestamp, message)
);
CREATE TABLE chatty_chat(
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  timestamp INTEGER NOT_NULL,
  direction INTEGER NOT NULL,
  account TEXT NOT NULL,
  room TEXT NOT_NULL,
  who TEXT,
  uid TEXT NOT_NULL,
  message TEXT,
  UNIQUE (timestamp, message)
);
CREATE UNIQUE INDEX chatty_im_acc_uid ON chatty_im(account, uid);
CREATE UNIQUE INDEX chatty_chat_room_uid ON chatty_chat(room, uid);

INSERT INTO chatty_chat VALUES(NULL,1586447316,1,'@alice:example.com','!VPWUCfyJyeVMxiHYGi:example.com','@bob:example.com','12107bfc-0f91-11eb-8501-2314b53187d5','Hi');
INSERT INTO chatty_chat VALUES(NULL,1586447319,1,'@alice:example.com','!VPWUCfyJyeVMxiHYGi:example.com','@bob:example.com','2a5f6c4a-0f91-11eb-af2c-27e3777f4483','Are you there?');
INSERT INTO chatty_chat VALUES(NULL,1586447320,1,'@bob:example.org','!CDFTfyJgtVMvsXDEi:example.com',NULL,'10600c18-ecc1-4d42-8f0a-5c5e563b1b3d','Another empty author message');
INSERT INTO chatty_chat VALUES(NULL,1586447421,-1,'alice','!VPWUCfyJyeVMxiHYGi:example.com','@alice:example.com','3a383ec7-7566-457b-b561-2145b328459c','Why?');
INSERT INTO chatty_chat VALUES(NULL,1586447419,-1,'@alice:example.com','!VPWUCfyJyeVMxiHYGi:example.com','@alice:example.com','6b67fa36-0f91-11eb-9714-af849160d937','Hi');
INSERT INTO chatty_chat VALUES(NULL,1586448429,1,'@bob:example.org','!CDFTfyJgtVMvsXDEi:example.com','@alice:example.com','c73bbcbc-0f91-11eb-aab2-8b95affe5e24','Test');
INSERT INTO chatty_chat VALUES(NULL,1586448432,1,'@bob:example.org','!CDFTfyJgtVMvsXDEi:example.com','@charlie:example.com','1dc29876-0f92-11eb-aeb4-d7486be58053','Failed');
INSERT INTO chatty_chat VALUES(NULL,1586448435,1,'@alice:example.com','!CDFTfyJgtVMvsXDEi:example.com','@_freenode_hunter2:example.com','414d35fa-e50f-441f-a382-3cb8acd7a510','Weird.  All I see is *');
INSERT INTO chatty_chat VALUES(NULL,1586448438,1,'@alice:example.com','!CDFTfyJgtVMvsXDEi:example.com',NULL,'f86768a5-d0fb-423c-9430-3d3b66d74a67','A message with no author');

COMMIT;
