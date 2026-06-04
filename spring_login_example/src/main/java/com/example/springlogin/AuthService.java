package com.example.springlogin;

import org.springframework.stereotype.Service;

@Service
public class AuthService {

    public int calls = 0;

    public boolean authenticate(String login, String password) {
        calls += 1;
        return "admin".equals(login) && "secret".equals(password);
    }
}
