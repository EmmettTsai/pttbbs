extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

# ifndef USE_VERIFYDB
#   error "USE_VERIFYDB_ACCOUNT_RECOVERY requires USE_VERIFYDB"
# endif

#include <optional>
#include <string>

#include "verifydb.h"
#include "verifydb.fbs.h"

namespace {

constexpr int kMaxErrCnt = 3;
constexpr size_t kCodeLen = 30;

struct UserHandle {
  std::string userid;
  int64_t generation;
};

class AccountRecovery {
public:
  void Run();

private:
  std::optional<UserHandle> user_;
  std::string email_;
  int y_ = 2;

  void InputUserEmail();
  void EmailCodeChallenge();
  void ResetPasswd();

  static std::optional<UserHandle> LoadUser(const char *userid);
  static bool CheckUserEmail(const UserHandle &user, const std::string &email,
                             bool *email_matches);
  static bool SendRecoveryCode(const std::string &email,
                               const std::string &code);
  static bool SetPasswd(const UserHandle &user, const char *hashed_passwd);
  static std::string GenCode(size_t len);
  static void UserErrorExit();
};

// static
std::optional<UserHandle> AccountRecovery::LoadUser(const char *userid) {
  if (!is_validuserid(userid))
    return std::nullopt;

  userec_t u = {};
  if (!getuser(userid, &u))
    return std::nullopt;

  // Double check.
  if (!is_validuserid(u.userid))
    return std::nullopt;

  UserHandle user;
  user.userid = u.userid;
  user.generation = u.firstlogin;
  return user;
}

// static
bool AccountRecovery::CheckUserEmail(const UserHandle &user,
                                     const std::string &email,
                                     bool *email_matches) {
  Bytes buf;
  const VerifyDb::GetReply *reply;
  if (!verifydb_getuser(user.userid.c_str(), user.generation, &buf, &reply))
    return false;

  *email_matches = false;
  if (reply->entry()) {
    for (const auto *ent : *reply->entry()) {
      if (ent->vmethod() == VMETHOD_EMAIL && ent->vkey() != nullptr &&
          ent->vkey()->str() == email) {
        *email_matches = true;
        break;
      }
    }
  }
  return true;
}

// static
bool AccountRecovery::SendRecoveryCode(const std::string &email,
                                       const std::string &code) {
  std::string subject;
  subject.append(" " BBSNAME " - ���]�K�X�{�ҽX [ ");
  subject.append(code);
  subject.append(" ] @ IP ");
  subject.append(fromhost);
  bsmtp("etc/recovermail", subject.c_str(), email.c_str(), "non-exist");
  return true;
}

// static
bool AccountRecovery::SetPasswd(const UserHandle &user,
                                const char *hashed_passwd) {
  userec_t u = {};
  int unum = getuser(user.userid.c_str(), &u);
  if (unum <= 0)
    return false;
  // Check userid and generation again.
  if (std::string(u.userid) != user.userid || u.firstlogin != user.generation)
    return false;
  strlcpy(u.passwd, hashed_passwd, sizeof(u.passwd));
  return 0 == passwd_sync_update(unum, &u);
}

// static
std::string AccountRecovery::GenCode(size_t len) {
  std::string s(len, '\0');
  random_text_code(&s[0], s.size());
  return s;
}

// static
void AccountRecovery::UserErrorExit() {
  vmsg("���~���ƹL�h�A�Э��s�ާ@�C");
  exit(0);
}

void AccountRecovery::InputUserEmail() {
  mvprints(y_++, 0, "�ثe�ȯ���^�ϥ� Email ���U���b���C");
  y_++;

  // Input userid.
  char userid[IDLEN + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "�����^�b��ID�G", userid, sizeof(userid), DOECHO);
    user_ = LoadUser(userid);
    if (user_)
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "�L�� ID�A�Э��s��J�C");
    userid[0] = '\0';
  }
  y_++;

  // Input email.
  char email[EMAILSZ] = {};
  errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "�{�ҫH�c�G", email, sizeof(email), DOECHO);
    strip_blank(email, email);
    str_lower(email, email);
    if (*email)
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "�Э��s��J�C");
    email[0] = '\0';
  }
  email_ = email;
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.
}

void AccountRecovery::EmailCodeChallenge() {
  // Generate code.
  auto code = GenCode(kCodeLen);

  // Check and send recovery code.
  bool email_matches = false;
  if (!CheckUserEmail(user_.value(), email_, &email_matches)) {
    vmsg("�t�ο��~�A�еy�ԦA�աC");
    exit(0);
  }

  y_++;
  mvprints(y_, 0, "�t�γB�z��...");
  doupdate();
  if (email_matches) {
    // We only want to send email if the user input the matching address.
    // Silently fail if email is not matching, so that user cannot guess other
    // user's email address.
    SendRecoveryCode(email_, code);
  }
  // Add a random 5-10s delay to prevent timing oracle.
  usleep(5000000 + random() % 5000000);
  mvprints(y_++, 0, "�Y�z��J����ƥ��T�A�t�Τw�N�{�ҽX�H�e�ܱz���H�c�C");

  // Input code.
  char incode[kCodeLen + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "�Ц��H���J�{�ҽX�G", incode, sizeof(incode), DOECHO);
    if (email_matches && code == std::string(incode))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "�{�ҽX���~�I�{�ҽX�@�� %d �r���C", (int)kCodeLen);
    incode[0] = '\0';
  }
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.

  // Some paranoid checkings, we are about to let user reset their password.
  if (code.size() != kCodeLen || code != std::string(incode)) {
    assert(false);
    exit(1);
  }
}

void AccountRecovery::ResetPasswd() {
  // Input new password.
  const char *hashed = nullptr;
  y_++;
  int errcnt = 0;
  while (1) {
    if (errcnt++ >= kMaxErrCnt * 2)
      UserErrorExit();

    char pass[PASS_INPUT_LEN + 1] = {};
    char confirm[PASS_INPUT_LEN + 1] = {};

    move(y_, 0);
    clrtobot();
    if (!getdata(y_, 0, "�г]�w�s�K�X�G", pass, sizeof(pass), PASSECHO) ||
        strlen(pass) < 4) {
      vmsg("�Э��s��J�A�K�X�����ܤ֥|�r��");
      continue;
    }

    move(y_ + 1, 0);
    outs("�Ъ`�N�]�w�K�X�u���e�K�Ӧr�����ġA�W�L���N�۰ʩ����C");

    getdata(y_ + 2, 0, "���ˬd�s�K�X�G", confirm, sizeof(confirm), PASSECHO);
    if (strcmp(pass, confirm)) {
      vmsg("�s�K�X�T�{����, �Э��s��J");
      continue;
    }

    // Hash password.
    hashed = genpasswd(pass);
    memset(pass, 0, sizeof(pass));
    memset(confirm, 0, sizeof(confirm));
    break;
  }
  assert(hashed);

  if (!SetPasswd(user_.value(), hashed))
    vmsg("�K�X���]���ѡA�еy�ԦA�աC");
  else
    vmsg("�K�X���]�����A�ХH�s�K�X�n�J�C");
}

void AccountRecovery::Run() {
  vs_hdr("���^�b��");
  InputUserEmail();
  EmailCodeChallenge();
  ResetPasswd();
}

}  // namespace

void recover_account() {
  AccountRecovery ac;
  ac.Run();
  exit(0);
}

#endif
