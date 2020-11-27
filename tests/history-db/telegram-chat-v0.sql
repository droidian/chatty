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

INSERT INTO chatty_chat VALUES(NULL,1502695424,1,'+19812121212','Random room',NULL,'c485ac17-513e-4e16-b049-dbc21e000ed8','Hi');
INSERT INTO chatty_chat VALUES(NULL,1502695426,1,'+19812121212','Random room',NULL,'3f5f7d60-1510-4249-80f4-ad802fa9483f','Hello');
INSERT INTO chatty_chat VALUES(NULL,1502695429,1,'+19812121212','Random room','New Person','26b5bd41-8f34-476a-bb03-9ed8f8129817','I''m New, Hi');
INSERT INTO chatty_chat VALUES(NULL,1502695429,1,'+19876543210','Random room','New Person','4d3defa2-85a2-4cd5-9e1b-940b2c406351','New here');
INSERT INTO chatty_chat VALUES(NULL,1502695432,1,'+19876543210','Random room','Random Person','955044fb-fc34-42a1-88c7-acdd0c45acc7','I''m random');
INSERT INTO chatty_chat VALUES(NULL,1502695432,1,'+19876543210','Another Room@example.com','Random Person','53269985-89da-4e01-9914-fa053735d59f','Another me');
INSERT INTO chatty_chat VALUES(NULL,1502695569,1,'+19876543210','Another Room@example.com','Bob','92a4e961-b3ac-487c-9dd6-c645944e5946','I''m bob');
INSERT INTO chatty_chat VALUES(NULL,1502695572,1,'+19876543210','Another Room@example.com',NULL,'be6ca8bf-b5d9-4983-bbd3-3767eda52f4a','I''m empty');
INSERT INTO chatty_chat VALUES(NULL,1502695573,-1,'+19876543210','Another Room@example.com',NULL,'1525c407-7c3d-4b02-8e26-a6e86183a8bc','Hello all');
INSERT INTO chatty_chat VALUES(NULL,1502695587,1,'+1 981 212 1212','Random room','Random Person','21fb7985-c3c4-4292-ab84-1b7c637c727a','Let me know who is here?');

COMMIT;
