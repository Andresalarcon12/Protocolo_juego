#ifndef AUTH_CLIENT_H
#define AUTH_CLIENT_H

//Retorna:
//1 = éxito
//0 = credenciales inválidas
//-1 = error de conexión
int auth_request(const char *host, const char *port,
                 const char *username, const char *password,
                 char *role_out);

#endif