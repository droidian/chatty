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

INSERT INTO chatty_im VALUES(NULL,1502685271,1,'+351-123-456-789','Random Person','01241679-58e4-4e65-b88f-67e70d617594','Hi');
INSERT INTO chatty_im VALUES(NULL,1502685274,1,'+351-123-456-789','Random Person','8a7ba154-9e09-4845-973e-cc6f8aedcdc5','Hello');
INSERT INTO chatty_im VALUES(NULL,1502685280,-1,'+351-123-456-789','Random Person','b23a7a25-7bdf-44ac-8685-d6881f3eaf90','Yeah');
INSERT INTO chatty_im VALUES(NULL,1502685282,-1,'+351123456789','Another Person','5a60ea9e-e6a0-4c5e-94bf-5e2330be4547','Hi');
INSERT INTO chatty_im VALUES(NULL,1502685284,1,'+351123456789','Another Person','dd12cdf6-0d8c-4010-8138-9640237ccc15','I''m here');
INSERT INTO chatty_im VALUES(NULL,1502685295,1,'+351123456789','Random Person','0887db8b-11f1-4167-9dfa-c8a4a0fad6d2','Can you call me @9:00?');
INSERT INTO chatty_im VALUES(NULL,1502685300,1,'+19876543210','Alice','84406650-c4a6-435d-ba4f-ac193b59a975','Hi');
INSERT INTO chatty_im VALUES(NULL,1502685303,-1,'+19876543210','Alice','e9d54317-9234-4de8-b345-c3a8e4d3b322','Hello');
INSERT INTO chatty_im VALUES(NULL,1502685304,1,'+1 987 654 3210','Alice','a88e7db7-3d41-4e3e-8e21-d1e4e6466a01','How are you');
INSERT INTO chatty_im VALUES(NULL,1502685403,-1,'+19876543210','Random Person','bf5b5a8c-e9bc-4c22-b215-bdb624c0524d','Hello Random');

COMMIT;
